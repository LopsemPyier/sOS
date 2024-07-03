#include "./policy_1.h"
#define max(a, b) ((a > b) ? a : b)

LIST_HEAD(freeList);
LIST_HEAD(usedList);

pthread_mutex_t policy1UsedLock;
pthread_mutex_t policy1FreeLock;


void put_resource_to_used_list(struct list_head* pages, unsigned long addr, int origin) {
    struct optEludeList *node, *node_s;
    list_for_each_entry_safe(node, node_s, pages, iulist) {
        node->resource->virtualResource = NULL;
        // node->resource->virtualId = addr;
        node->resource->process = origin;
        pthread_mutex_lock(&policy1UsedLock);
        list_move_tail(&node->iulist, &usedList);
        pthread_mutex_unlock(&policy1UsedLock);
        addr += VIRTUAL_RESOURCE_SIZE;
    }
}

void put_resource_back_to_free_list(struct optEludeList * resource) {
    pthread_mutex_lock(&policy1FreeLock);

    list_move_tail(&resource->iulist, &freeList);

    pthread_mutex_unlock(&policy1FreeLock);
}

void give_back_resource(struct list_head * resources) {
    struct list_head *resourceIter, *resourceIter_s;
    pthread_mutex_lock(&policy1FreeLock);

    list_for_each_safe(resourceIter, resourceIter_s, resources) {
        list_move_tail(resourceIter, &freeList);
    }

    pthread_mutex_unlock(&policy1FreeLock);
}


void *resource_thread(void* p_args) {
    int origin=((struct p_args_p*)p_args)->origin, ret=((struct p_args_p*)p_args)->ret;
    unsigned long addr=((struct p_args_p*)p_args)->addr;
    struct list_head *l_ps=((struct p_args_p*)p_args)->l_ps;
    free(p_args);

    if(unlikely(ret)){
        give_back_resource(l_ps);
    }
    else
        put_resource_to_used_list(l_ps,addr,origin);
}

int get_resources_ready_to_assign(struct list_head* resource_list, int requested, struct sOSEvent* event) {
    int remaining = requested;

    struct optEludeList * chosenResource;
    pthread_mutex_lock(&policy1FreeLock);
    bool first = true;
    while (remaining-- > 0) {
        if (unlikely(list_empty(&freeList))) {
            pthread_mutex_unlock(&policy1FreeLock);
            return requested - remaining + 1;
        }
        chosenResource = list_first_entry(&freeList, struct optEludeList, iulist);
        list_move_tail(&chosenResource->iulist, resource_list);
        if (first) {
            event->physical_id = chosenResource->resource->physicalId;
            first = false;
        }
    }
    pthread_mutex_unlock(&policy1FreeLock);

    return requested;
}


static inline int policy1_select_phys_to_virtual(struct sOSEvent *event) {
    trace("TRACE: entering policy1::select_phys_to_virt\n");

    if (list_empty(&freeList)) {
        trace("TRACE: exiting policy1::select_phys_to_virt -- error empty free list\n");
        return 1;
    }

    LIST_HEAD(tmpList);

    pthread_mutex_lock(&policy1FreeLock);

    struct optEludeList * chosenResource = list_first_entry(&freeList, struct optEludeList, iulist);

    list_move_tail(&chosenResource->iulist, &tmpList);
    pthread_mutex_unlock(&policy1FreeLock);
    pthread_mutex_lock(&policy1UsedLock);
    list_move_tail(&chosenResource->iulist, &usedList);
    pthread_mutex_unlock(&policy1UsedLock);

    event->physical_id = chosenResource->resource->physicalId;
    chosenResource->resource->virtualResource = NULL;
    // chosenResource->resource->virtualId = event->virtual_id;
    chosenResource->resource->process = event->attached_process;

    trace("TRACE: exiting policy1::select_phys_to_virt\n");

    return 0;


}

static inline int policy1_select_virtual_to_evict(struct sOSEvent* event) {
    trace("TRACE: entering policy1::select_virtual_to_evict\n");
    trace("TRACE: exiting policy1::select_virtual_to_evict\n");
    return 0;
}

static inline int policy1_select_virtual_to_load(struct sOSEvent* event) {
    trace("TRACE: entering policy1::select_virtual_to_load\n");
    trace("TRACE: exiting policy1::select_virtual_to_load\n");
    return 0;
}

static inline int policy1_save_context(struct sOSEvent* event) {
    trace("TRACE: entering policy1::save_context\n");
    trace("TRACE: exiting policy1::save_context\n");
    return 0;
}

static inline int policy1_restore_context(struct sOSEvent* event) {
    trace("TRACE: entering policy1::restore_context\n");
    trace("TRACE: exiting policy1::restore_context\n");
    return 0;
}

static inline int policy1_on_yield(struct sOSEvent *event) {
    trace("TRACE: entering policy1::on_yield\n");

    struct optEludeList *resource_yielded;
    pthread_mutex_lock(&policy1UsedLock);
    list_for_each_entry(resource_yielded, &usedList, iulist) {
        if (resource_yielded->resource->virtualId == event->virtual_id) {
            break;
        }
    }

    if (&resource_yielded->iulist == &usedList) {
        trace("TRACE: exiting policy1::on_yield -- error\n");
        return 1;
    }

    LIST_HEAD(tmpList);
    list_move(&resource_yielded->iulist, &tmpList);
    pthread_mutex_unlock(&policy1UsedLock);

    pthread_mutex_lock(&policy1FreeLock);
    list_move(&resource_yielded->iulist, &freeList);
    pthread_mutex_unlock(&policy1FreeLock);
    resource_yielded->resource->virtualId = 0;
    resource_yielded->resource->process = 0;
    resource_yielded->resource->processUsedListPointer = NULL;

    trace("TRACE: exiting policy1::on_yield\n");
    return 0;
}

static inline int policy1_on_ready(struct sOSEvent* event) {
    trace("TRACE: entering policy1::on_ready\n");
    trace("TRACE: exiting policy1::on_ready\n");
    return 0;
}

static inline int policy1_on_invalid(struct sOSEvent* event) {
    trace("TRACE: entering policy1::on_invalid\n");
    trace("TRACE: exiting policy1::on_invalid\n");
    return 0;
}

static inline int policy1_on_hints(struct sOSEvent* event) {
    trace("TRACE: entering policy1::on_hints\n");
    trace("TRACE: exiting policy1::on_hints\n");
    return 0;
}

static inline int policy1_on_protection_violation(struct sOSEvent* event) {
    trace("TRACE: entering policy1::on_protection_violation\n");
    trace("TRACE: exiting policy1::on_protection_violation\n");
    return 0;
}

static inline int policy1_on_create_thread(struct sOSEvent* event) {
    trace("TRACE: entering policy1::on_create_thread\n");
    trace("TRACE: exiting policy1::on_create_thread\n");
    return 0;
}

static inline int policy1_on_dead_thread(struct sOSEvent* event) {
    trace("TRACE: entering policy1::on_dead_thread\n");
    trace("TRACE: exiting policy1::on_dead_thread\n");
    return 0;
}

static inline int policy1_on_sleep_state_change(struct sOSEvent* event) {
    trace("TRACE: entering policy1::on_sleep_state_change\n");
    trace("TRACE: exiting policy1::on_sleep_state_change\n");
    return 0;
}

static inline int policy1_on_signal(struct sOSEvent* event) {
    trace("TRACE: entering policy1::on_signal\n");
    trace("TRACE: exiting policy1::on_signal\n");
    return 0;
}

static inline int policy1_init(unsigned long numberOfResource) {
    trace("TRACE: entering policy1::init\n");

    for (int i = 0; i < numberOfResource; i++) {
        struct optEludeList *freeListNode=(struct optEludeList *)malloc(sizeof(struct optEludeList));
        freeListNode->resource=resourceList+i;
        resourceList[i].usedListPositionPointer=freeListNode;
        INIT_LIST_HEAD(&(freeListNode->iulist));
        list_add_tail(&(freeListNode->iulist),&freeList);
    }
    pthread_mutex_init(&policy1FreeLock,NULL);
    pthread_mutex_init(&policy1UsedLock,NULL);

    trace("TRACE: exiting policy1::init\n");
    return 0;
}


void policy1_exit() {
    trace("TRACE: entering policy1::exit\n");

    struct list_head *listIter, *listIter_s;
    list_for_each_safe(listIter, listIter_s, &freeList) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }
    list_for_each_safe(listIter, listIter_s, &usedList) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    trace("TRACE: exiting policy1::exit\n");
}

struct policy_function policy_1_functions = {
        .select_phys_to_virtual = &policy1_select_phys_to_virtual,
        .select_virtual_to_evict = &policy1_select_virtual_to_evict,
        .select_virtual_to_load = &policy1_select_virtual_to_load,
        .save_context = &policy1_save_context,
        .restore_context = &policy1_restore_context,

        .on_yield = &policy1_on_yield,
        .on_ready = &policy1_on_ready,
        .on_invalid = &policy1_on_invalid,
        .on_hints = &policy1_on_hints,
        .on_protection_violation = &policy1_on_protection_violation,
        .on_create_thread = &policy1_on_create_thread,
        .on_dead_thread = &policy1_on_dead_thread,
        .on_sleep_state_change = &policy1_on_sleep_state_change,
        .on_signal = &policy1_on_signal,

        .init = &policy1_init,
        .exit = &policy1_exit,
};

struct policy_detail policy1_detail = {
        .name = "policy1",
        .functions = &policy_1_functions,
        .is_default = false
};