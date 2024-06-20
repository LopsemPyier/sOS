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
#define wrap_policy(name, policy) \
usm_alloc(name, policy) \
usm_free(name, policy) \
usm_pindex_free(name, policy)

#define usm_ops(name) {.usm_alloc=&alloc_function_name(name),.usm_pindex_free=&pindex_free_function_name(name),.usm_free=&free_function_name(name)}

#define usm_alloc_policy_ops_name(name) usm_alloc_##name##_ops

#define create_usm_bindings(name,policy) \
wrap_policy(name, policy.functions)     \
struct usm_alloc_policy_ops usm_alloc_policy_ops_name(name) = usm_ops(name);
#define register_policy(policy_name,policy) \
if (usm_register_alloc_policy(&usm_alloc_policy_ops_name(policy_name), policy.name, policy.is_default)) \
    return 1;

#define min(a, b) ((a < b) ? a : b)

struct resource* resourceList = NULL;
LIST_HEAD(eventList);

static inline int usm_alloc_impl(struct usm_event *usmEvent, struct policy_function *policy) {
    trace("TRACE: entering sOS::API::usm_alloc_impl\n");

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    struct optEventList* optEvent = (struct optEventList*) malloc(sizeof(struct optEventList));

    optEvent->event = usmEvent;

    INIT_LIST_HEAD(&optEvent->iulist);
    list_add_tail(&(optEvent->iulist), &eventList);

    event->attached_process = usmEvent->origin;
    event->virtual_id = usmEvent->vaddr;
    event->eventPosition = &(optEvent->iulist);

    printf("Created sOSEvent\n");

    unsigned long av = 0;
    unsigned long req = min(usmEvent->length, 512);
    int vpfn = -1, pfn, pos = 0;
    while (req-- > 0) {
        if (policy->select_phys_to_virtual(event)) {
            trace("TRACE: exiting sOS::API::usm_alloc_impl -- error\n");
            return 1;
        }

        if (vpfn == -1) {
            vpfn = event->physical_id;
            usmEvent->paddr = event->physical_id;
            usmPrepPreAlloc(usmEvent, vpfn++, pos)
        } else {
            pfn = event->physical_id;

            if (likely(vpfn == pfn)) {
                count ++;
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
    }

    if (likely(count)) {
        usmPrepPreAllocFastCounted(usmEvent, pos, count);
    }

    usmEvent->length = av;

    int ret = usmSubmitAllocEvent(usmEvent);

    trace("TRACE: exiting sOS::API::usm_alloc_impl\n");
    return 0;
}



static inline int usm_pindex_free_impl(struct usm_event *usmEvent, struct policy_function *policy) {
    return 0;
}


static inline int usm_free_impl(struct usm_event *usmEvent, struct policy_function *policy) {
    trace("TRACE: entering sOS::API::usm_free_impl\n");

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    event->attached_process = usmEvent->origin;
    event->virtual_id = usmEvent->vaddr;
    event->virtual_nb = usmEvent->length;

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

create_usm_bindings(policy1, policy1_detail)


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

    register_policy(policy1, policy1_detail)

    if (policy1_detail.functions->init)
        policy1_detail.functions->init(pagesNumber);

    get_pages=&get_pages_impl;
    put_pages=&put_pages_impl;
    restore_used_page=&restore_used_pages_impl;
    hold_used_page=&hold_used_pages_impl;

    trace("TRACE: exiting sOS::API::setup\n");
    return 0;
}
void policy_alloc_exit() {
    trace("TRACE: entering sOS::API::setup\n");

    if (policy1_detail.functions->exit)
        policy1_detail.functions->exit();

    trace("TRACE: exiting sOS::API::setup\n");
}


struct usm_ops dev_usm_ops = {
        usm_setup:
        &policy_alloc_setup,
        usm_free:
        &policy_alloc_exit
};
