#include "../incl/linkedApi.h"

#define alloc_function_name(name) usm_alloc_##name
#define usm_alloc(name, policy) \
static inline int alloc_function_name(name) (struct usm_event *usmEvent) { \
    return usm_alloc_impl(usmEvent, policy); \
}
#define free_function_name(name) usm_free_##name
#define usm_free(name, policy) \
static inline int free_function_name(name) (struct usm_event *usmEvent) { \
    return usm_free_impl(usmEvent, policy); \
}
#define pindex_free_function_name(name) usm_pindex_free_##name
#define usm_pindex_free(name, policy) \
static inline int pindex_free_function_name(name) (struct usm_event *usmEvent) { \
    return usm_pindex_free_impl(usmEvent, policy); \
}
#define hints_function_name(name) usm_hints_##name
#define usm_hints(name, policy) \
static inline int hints_function_name(name) (struct usm_event *usmEvent) { \
    return usm_hints_impl(usmEvent, policy); \
}
#define process_state_change_function_name(name) usm_process_state_change_##name
#define usm_process_state_change(name, policy) \
static inline int process_state_change_function_name(name) (struct usm_event *usmEvent) { \
    return usm_process_state_change_impl(usmEvent, policy); \
}
#define new_process_function_name(name) usm_new_process_##name
#define usm_new_process(name, policy) \
static inline int new_process_function_name(name) (struct usm_event *usmEvent) { \
    return usm_new_process_impl(usmEvent, policy); \
}
#define wrap_policy(name, policy) \
usm_alloc(name, policy) \
usm_free(name, policy) \
usm_pindex_free(name, policy) \
usm_hints(name, policy) \
usm_process_state_change(name, policy) \
usm_new_process(name, policy)

#define usm_ops(name) {.usm_alloc=&alloc_function_name(name),.usm_pindex_free=&pindex_free_function_name(name),.usm_free=&free_function_name(name),.usm_hints=&hints_function_name(name),.usm_process_state_change=&process_state_change_function_name(name),.usm_new_process=&new_process_function_name(name)}

#define usm_alloc_policy_ops_name(name) usm_alloc_##name##_ops

#define create_usm_bindings(name,policy) \
wrap_policy(name, policy.functions)     \
struct usm_alloc_policy_ops usm_alloc_policy_ops_name(name) = usm_ops(name);
#define register_policy(policy_name,policy) \
if (usm_register_alloc_policy(&usm_alloc_policy_ops_name(policy_name), policy.name, policy.is_default)) \
    return 1;

#define min(a, b) ((a < b) ? a : b)

#define DEFAULT_HOTNESS 1000000

struct resource* resourceList = NULL;
LIST_HEAD(eventList);

static inline int usm_alloc_impl(struct usm_event *usmEvent, struct policy_function *policy) {
    trace("TRACE: entering sOS::API::usm_alloc_impl\n");

    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &time);

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    struct HintsPayload *hintsPayload = (struct HintsPayload*) malloc(sizeof(struct HintsPayload));
    hintsPayload->type = UTILISATION;
    hintsPayload->utilisation = DEFAULT_HOTNESS;

    event->attached_process = usmEvent->origin;
    event->virtual_id = usmEvent->vaddr;
    event->event_id = (unsigned long) usmEvent->usmmem;

    printf("Created sOSEvent\n");
    printf("Allocation of vaddr %ld to paddr %ld\n", usmEvent->vaddr, usmEvent->paddr);

    unsigned long av = 0;
    unsigned long req = min(usmEvent->length+1, 512);
    usmEvent->paddr = 0;
    int vpfn = -1, pfn, pos = 0, count = 0;
    while (req > 0) {
        policy->on_create_thread(event);
        policy->on_ready(event);
        if (policy->select_phys_to_virtual(event)) {
            free(hintsPayload);
            free(event);
            trace("TRACE: exiting sOS::API::usm_alloc_impl -- error\n");
            return 1;
        }

        printf("Associating virtual_id %lu to physical_id %lu\n", event->virtual_id, event->physical_id);

        if (policy->on_hints(event, hintsPayload)) {
            trace("TRACE: sOS::API::usm_alloc_impl -- error on hints\n");
        }

        struct page* page = get_usm_page_from_paddr(event->physical_id);
        page->virtualAddress = event->virtual_id;
        page->process = event->attached_process;

        if (usmEvent->paddr == 0) {
            usmEvent->paddr = event->physical_id;
        } else if (vpfn == -1) {
            vpfn = event->physical_id;
            usmPrepPreAlloc(usmEvent, vpfn++, pos);
        } else {
            pfn = event->physical_id;

            if (likely(vpfn == pfn)) {
                count++;
            } else {
                usmPrepPreAllocFastCounted(usmEvent, pos, count);
                usmPrepPreAlloc(usmEvent, pfn, pos);
                count = 0;
            }
            vpfn = pfn + 1;
        }

        event->virtual_id += VIRTUAL_RESOURCE_SIZE;
        event->physical_id = 0;
        av++;
        req--;
    }

    if (likely(count)) {
        usmPrepPreAllocFastCounted(usmEvent, pos, count);
    }

    usmEvent->length = av-1;

    int ret = usmSubmitAllocEvent(usmEvent);

    free(hintsPayload);
    free(event);

    trace("TRACE: exiting sOS::API::usm_alloc_impl\n");
    return 0;
}



static inline int usm_pindex_free_impl(struct usm_event *usmEvent, struct policy_function *policy) {
    trace("TRACE: entering sOS::API::usm_pindex_free_impl\n");

    struct page * usmPage = usmEventToPage(usmEvent);

    if (!usmPage) {
        trace("TRACE: exiting sOS::API::usm_pindex_free_impl -- error no page found\n");
        return 1;
    }

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    event->attached_process = usmEvent->origin;
    event->physical_id = usmPage->physicalAddress;
    event->virtual_id = usmPage->virtualAddress;

    if (policy->on_yield(event)) {
        trace("TRACE: exiting sOS::API::usm_pindex_free_impl -- error\n");
        return 1;
    }

    memset((void*)(usmPage->data), 0, 4096);

    trace("TRACE: exiting sOS::API::usm_pindex_free_impl\n");
    return 0;
}

static inline int usm_hints_impl(struct usm_event *usmEvent, struct policy_function *policy) {
    trace("TRACE: entering sOS::API::usm_hints_impl\n");

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));
    struct HintsPayload *hintsPayload = (struct HintsPayload*) malloc(sizeof(struct HintsPayload));

    event->attached_process = usmEvent->origin;
    event->virtual_id = usmEvent->vaddr;

    if (usmEvent->hintsType == MLOCK) {
        hintsPayload->type = PRIORITY;
        hintsPayload->priority = usmEvent->priority;
    } else if (usmEvent->hintsType == HOTNESS) {
        hintsPayload->type = UTILISATION;
        hintsPayload->utilisation = -usmEvent->hotness;
    }

    if (policy->on_hints(event, hintsPayload)) {
        free(event);
        free(hintsPayload);
        trace("TRACE: exiting sOS::API::usm_hints_impl -- error\n");
        return 1;
    }
    free(event);
    free(hintsPayload);

    trace("TRACE: exiting sOS::API::usm_hints_impl\n");
    return 0;
}

static inline int usm_process_state_change_impl(struct usm_event *usmEvent, struct policy_function *policy) {
    trace("TRACE: entering sOS::API::usm_process_state_change_impl\n");

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    event->attached_process = usmEvent->origin;
    //event->virtual_id = usmEvent->vaddr;

    if (usmEvent->type == PROC_DTH || usmEvent->type == THREAD_DTH) {
        if (policy->on_dead_thread(event)) {
            free(event);
            trace("TRACE: exiting sOS::API::usm_process_state_change_impl -- error\n");
            return 1;
        }
    }

    free(event);

    trace("TRACE: exiting sOS::API::usm_process_state_change_impl\n");
    return 0;
}

static inline int usm_new_process_impl(struct usm_event *usmEvent, struct policy_function *policy) {
    trace("TRACE: entering sOS::API::usm_new_process_impl\n");

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    event->attached_process = usmEvent->origin;
    event->virtual_id = usmEvent->vaddr;

/*    if (policy->on_create_thread(event)) {
        free(event);
        trace("TRACE: exiting sOS::API::usm_new_process_impl -- error\n");
        return 1;
    }*/
    free(event);

    trace("TRACE: exiting sOS::API::usm_new_process_impl\n");
    return 0;
}


static inline int usm_free_impl(struct usm_event *usmEvent, struct policy_function *policy) {
    trace("TRACE: entering sOS::API::usm_free_impl\n");

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    event->attached_process = usmEvent->origin;
    event->virtual_id = usmEvent->vaddr;

    if (policy->on_yield(event)) {
        trace("TRACE: exiting sOS::API::usm_free_impl -- error\n");
        return 1;
    }

    trace("TRACE: exiting sOS::API::usm_free_impl\n");
    return 0;
}

static inline int get_pages_impl(struct list_head* placeholder, int nr) {
    trace("TRACE: entering sOS::API::get_pages\n");
    trace("TRACE: exiting sOS::API::get_pages\n");
    return 0;
}

static inline void put_pages_impl(struct list_head* pages) {
    trace("TRACE: entering sOS::API::put_pages\n");
    trace("TRACE: exiting sOS::API::put_pages\n");
}

static inline void restore_used_pages_impl(struct list_head* pages) {
    trace("TRACE: entering sOS::API::restore_used_pages\n");
    trace("TRACE: exiting sOS::API::restore_used_pages\n");
}

static inline void hold_used_pages_impl(struct list_head* pages) {
    trace("TRACE: entering sOS::API::hold_used_pages\n");
    trace("TRACE: exiting sOS::API::hold_used_pages\n");
}

create_usm_bindings(fifo, fifo_policy_detail)
create_usm_bindings(round_robin, rr_policy_detail)
create_usm_bindings(cfs, cfs_policy_detail)


static inline void initResources(unsigned long resourceSize) {
    trace("TRACE: entering sOS::API::initResources\n");

    resourceList = (struct resource *) malloc(sizeof(struct resource) * resourceSize);
    for (unsigned int i = 0; i < resourceSize; i++) {
        insertResource(&resourceList, pagesList[i].physicalAddress, i);
    }
    trace("TRACE: exiting sOS::API::initResources\n");
}

int policy_alloc_setup(unsigned int pagesNumber) {
    trace("TRACE: entering sOS::API::setup\n");
    initResources(pagesNumber);
    init_queues();

    printf("Page Size %ld\n", SYS_PAGE_SIZE);

    register_policy(fifo, fifo_policy_detail)
    if (fifo_policy_detail.functions->init) {
        fifo_policy_detail.functions->init(pagesNumber);
    }

    register_policy(round_robin, rr_policy_detail)
    if (rr_policy_detail.functions->init) {
        rr_policy_detail.functions->init(pagesNumber);
    }

    register_policy(cfs, cfs_policy_detail)
    if (cfs_policy_detail.functions->init) {
        cfs_policy_detail.functions->init(pagesNumber);
    }

    /*struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof (struct sOSEvent));
    for (unsigned int i = 0; i < pagesNumber; i++) {
        event->physical_id = pagesList[i].physicalAddress;
        event->sleep = AVAILABLE;
        cfs_policy_detail.functions->on_sleep_state_change(event);
    }
    free(event); */

    get_pages=&get_pages_impl;
    put_pages=&put_pages_impl;
    restore_used_page=&restore_used_pages_impl;
    hold_used_page=&hold_used_pages_impl;

    trace("TRACE: exiting sOS::API::setup\n");
    return 0;
}
void policy_alloc_exit() {
    trace("TRACE: entering sOS::API::setup\n");

    if (fifo_policy_detail.functions->exit)
        fifo_policy_detail.functions->exit();

    if (rr_policy_detail.functions->exit)
        rr_policy_detail.functions->exit();

    if (cfs_policy_detail.functions->exit)
        cfs_policy_detail.functions->exit();

    trace("TRACE: exiting sOS::API::setup\n");
}


struct usm_ops dev_usm_ops = {
        usm_setup:
        &policy_alloc_setup,
        usm_free:
        &policy_alloc_exit
};
