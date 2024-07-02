//
// Created by nathan on 01/07/24.
//

#include "cfs_policy.h"

LIST_HEAD(_cfs_virtual_on_ressource_queue);
LIST_HEAD(_cfs_virtual_valid_queue);
LIST_HEAD(_cfs_virtual_invalid_queue);

LIST_HEAD(_cfs_physical_free_list);
LIST_HEAD(_cfs_physical_used_list);

pthread_mutex_t _cfs_resourceListLock;
pthread_mutex_t _cfs_virtualOnResourceQueueLock;
pthread_mutex_t _cfs_virtualValidQueueLock;
pthread_mutex_t _cfs_virtualInvalidQueueLock;
pthread_mutex_t _cfs_physicalUsedListLock;
pthread_mutex_t _cfs_physicalFreeListLock;
int _cfs_nb_resources

void _cfs_insert_virtual_resource_by_utilisation(struct optVirtualResourceList* resource, struct list_head* lst) {
    struct optVirtualResourceList* item;
    list_for_each_entry(item, lst, iulist) {
        if (item->resource->utilisation > resource->resource->utilisation) {
            list_move(&resource->iulist, &item->iulist);
            return;
        }
    }
}

void _cfs_move_to_valid_queue_safe(struct optVirtualResourceList* resource, pthread_mutex_t* lock) {
    LIST_HEAD(tmp);

    pthread_mutex_lock(lock);
    list_move_tail(item, &tmp);
    pthread_mutex_unlock(lock);

    pthread_mutex_lock(&_cfs_virtualValidQueueLock);
    _cfs_insert_virtual_resource_by_utilisation(item, &_cfs_virtual_valid_queue);
    pthread_mutex_unlock(&_cfs_virtualValidQueueLock);
}

// Pre-computed weight values for each nice value. The weight values are
// the same as ones defined in `/kernel/sched/core.c`. The values are
// scaled with respect to 1024 for nice value 0.
static unsigned long weights[40] = {
    88761, 71755, 56483, 46273, 36291,  // -20 .. -16
    29154, 23254, 18705, 14949, 11916,  // -15 .. -11
    9548,  7620,  6100,  4904,  3906,   // -10 .. -6
    3121,  2501,  1991,  1586,  1277,   // -5 .. -1
    1024,  820,   655,   526,   423,    // 0 .. 4
    335,   272,   215,   172,   137,    // 5 .. 9
    110,   87,    70,    56,    45,     // 10 .. 14
    36,    29,    23,    18,    15      // 15 .. 19
};

// Pre-computed inverse weight values for each nice value (2^32/weight). The
// inverse weight values are the same as ones defined in
// `/kernel/sched/core.c`. These values are to transform division by the
// weight values into multiplication by the inverse weights, which works
// better for integers.
static unsigned long inverted_weights[40] = {
    48388,     59856,     76040,     92818,     118348,    // -20 .. -16
    147320,    184698,    229616,    287308,    360437,    // -15 .. -11
    449829,    563644,    704093,    875809,    1099582,   // -10 .. -6
    1376151,   1717300,   2157191,   2708050,   3363326,   // -5 .. -1
    4194304,   5237765,   6557202,   8165337,   10153587,  // 0 .. 4
    12820798,  15790321,  19976592,  24970740,  31350126,  // 5 .. 9
    39045157,  49367440,  61356676,  76695844,  95443717,  // 10 .. 14
    119304647, 148102320, 186737708, 238609294, 286331153  // 15 .. 19
};


static inline int cfs_policy_select_phys_to_virtual(struct sOSEvent *event);
static inline int cfs_policy_select_virtual_to_evict(struct sOSEvent *event); // TODO:
static inline int cfs_policy_select_virtual_to_load(struct sOSEvent *event);
static inline int cfs_policy_save_context(struct sOSEvent *event); // TODO:
static inline int cfs_policy_restore_context(struct sOSEvent *event); // TODO:
static inline int cfs_policy_on_yield(struct sOSEvent *event);
static inline int cfs_policy_on_ready(struct sOSEvent *event);
static inline int cfs_policy_on_invalid(struct sOSEvent *event);
static inline int cfs_policy_on_hints(struct sOSEvent *event); // TODO:
static inline int cfs_policy_on_protection_violation(struct sOSEvent *event); // TODO:
static inline int cfs_policy_on_create_thread(struct sOSEvent *event);
static inline int cfs_policy_on_dead_thread(struct sOSEvent *event);
static inline int cfs_policy_on_sleep_state_change(struct sOSEvent *event); // TODO:
static inline int cfs_policy_on_signal(struct sOSEvent *event); // TODO:
static inline int cfs_policy_init(unsigned long numberOfResource);
static inline void cfs_policy_exit();

struct policy_function cfs_policy_functions = {
        .select_phys_to_virtual = &cfs_policy_select_phys_to_virtual,
        .select_virtual_to_evict = &cfs_policy_select_virtual_to_evict,
        .select_virtual_to_load = &cfs_policy_select_virtual_to_load,
        .save_context = &cfs_policy_save_context,
        .restore_context = &cfs_policy_restore_context,

        .on_yield = &cfs_policy_on_yield,
        .on_ready = &cfs_policy_on_ready,
        .on_invalid = &cfs_policy_on_invalid,
        .on_hints = &cfs_policy_on_hints,
        .on_protection_violation = &cfs_policy_on_protection_violation,
        .on_create_thread = &cfs_policy_on_create_thread,
        .on_dead_thread = &cfs_policy_on_dead_thread,
        .on_sleep_state_change = &cfs_policy_on_sleep_state_change,
        .on_signal = &cfs_policy_on_signal,

        .init = &cfs_policy_init,
        .exit = &cfs_policy_exit,
};

struct policy_detail cfs_policy_detail = {
        .name = "cfsPolicy",
        .functions = &cfs_policy_functions,
        .is_default = false
};

struct optVirtualResourceList* _cfs_get_virtual_resource(struct list_head* list, unsigned long virtualId) {
    struct optVirtualResourceList *virtualResource;
    list_for_each_entry(virtualResource, list, iulist) {
        if (virtualResource->resource->virtualId == virtualId) {
            break;
        }
    }
    if (&virtualResource->iulist == list)
        return NULL;
    else
        return virtualResource;
}

struct optEludeList* _cfs_get_physical_resource(struct list_head* list, unsigned long physicalId) {
    struct optEludeList *physicalResource;
    list_for_each_entry(physicalResource, list, iulist) {
        if (physicalResource->resource->physicalId == physicalId) {
            break;
        }
    }
    if (&physicalResource->iulist == list)
        return NULL;
    else
        return physicalResource;
}

void _cfs_move_list_safe(struct list_head * item, struct list_head * dest, pthread_mutex_t* lock1, pthread_mutex_t* lock2) {
    LIST_HEAD(tmp);

    pthread_mutex_lock(lock1);
    list_move_tail(item, &tmp);
    pthread_mutex_unlock(lock1);

    pthread_mutex_lock(lock2);
    list_move_tail(item, dest);
    pthread_mutex_unlock(lock2);
}

void _cfs_put_virtual_on_physical(struct optVirtualResourceList* virtual, struct optEludeList* physical) {
    _cfs_move_list_safe(&virtual->iulist, &_cfs_virtual_on_ressource_queue, &_cfs_virtualValidQueueLock, &_cfs_virtualOnResourceQueueLock);
    _cfs_move_list_safe(&physical->iulist, &_cfs_physical_used_list, &_cfs_physicalFreeListLock, &_cfs_physicalUsedListLock);

    pthread_mutex_lock(&physical->resource->lock);
    physical->resource->virtualResource = virtual;
    pthread_mutex_unlock(&physical->resource->lock);
    virtual->resource->physical_resource = physical;
}

void _cfs_put_virtual_off_physical(struct optVirtualResourceList* virtual, struct optEludeList* physical) {
    _cfs_move_to_valid_queue_safe(virtual, &_cfs_virtualOnResourceQueueLock);
    _cfs_move_list_safe(&physical->iulist, &_cfs_physical_free_list, &_cfs_physicalUsedListLock, &_cfs_physicalFreeListLock);

    pthread_mutex_lock(&physical->resource->lock);
    physical->resource->virtualResource = NULL;
    pthread_mutex_unlock(&physical->resource->lock);
    virtual->resource->physical_resource = NULL;
}


static inline int cfs_policy_select_phys_to_virtual(struct sOSEvent *event) {
    trace("TRACE: entering cfs_policy::select_phys_to_virt\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = _cfs_get_virtual_resource(&_cfs_virtual_valid_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting cfs_policy::select_phys_to_virt -- error virtual not found\n");
        return 1;
    }

    if (list_empty(&_cfs_physical_free_list)) {
        trace("TRACE: exiting cfs_policy::select_phys_to_virt -- error no free physical\n");
        return 1;
    }


    struct optEludeList * phys = list_first_entry(&_cfs_physical_free_list, struct optEludeList, iulist);

    _cfs_put_virtual_on_physical(virtualResource, phys);

    event->physical_id = phys->resource->physicalId;
    unsigned long tmp = virtualResource->resource->last_event_id;
    virtualResource->resource->last_event_id = event->event_id;

    event->event_id = tmp;

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SELECT_PHYS_TO_VIRTUAL, start, end);

    trace("TRACE: exiting cfs_policy::select_phys_to_virt\n");
    return 0;
}

static inline int cfs_policy_select_virtual_to_evict(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::select_virtual_to_evict\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SELECT_VIRTUAL_TO_EVICT, start, end);

    trace("TRACE: exiting cfs_policy::select_virtual_to_evict\n");
    return 0;
}

static inline int cfs_policy_select_virtual_to_load(struct sOSEvent* event) {
    // trace("TRACE: entering cfs_policy::select_virtual_to_load\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optEludeList * physicalResource = _cfs_get_physical_resource(&_cfs_physical_used_list, event->physical_id);

    if (physicalResource && physicalResource->resource->virtualResource) { // TODO: Add preemption time
//        trace("TRACE: exiting cfs_policy::select_virtual_to_load -- already in use\n");
        return 1;
    }

    if (list_empty(&_cfs_virtual_valid_queue)) {
        // trace("TRACE: exiting cfs_policy::select_virtual_to_load -- empty\n");
        return 1;
    }

    struct optVirtualResourceList* virtualResource = list_first_entry(&_cfs_virtual_valid_queue, struct optVirtualResourceList, iulist);

    physicalResource = _cfs_get_physical_resource(&_cfs_physical_free_list, event->physical_id);

    if (!physicalResource) {
        trace("TRACE: exiting cfs_policy::select_virtual_to_load -- error\n");
        return 0;
    }

    _cfs_put_virtual_on_physical(virtualResource, physicalResource);

    event->virtual_id = virtualResource->resource->virtualId;
    event->event_id = virtualResource->resource->last_event_id;

    printf("Selected task %lu to assigned to cpu %lu\n", event->virtual_id, event->physical_id);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SELECT_VIRTUAL_TO_LOAD, start, end);
    trace("TRACE: exiting cfs_policy::select_virtual_to_load\n");
    return 0;
}

static inline int cfs_policy_save_context(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::save_context\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = _cfs_get_virtual_resource(&_cfs_virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting cfs_policy::save_context -- error virtual not found\n");
        return 1;
    }

    struct optEludeList * physicalResource = _cfs_get_physical_resource(&_cfs_physical_used_list, event->physical_id);

    if (!physicalResource) {
        trace("TRACE: exiting cfs_policy::save_context -- error physical not found\n");
        return 0;
    }

    _cfs_put_virtual_off_physical(virtualResource, physicalResource);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SAVE_CONTEXT, start, end);

    trace("TRACE: exiting cfs_policy::save_context\n");
    return 0;
}

static inline int cfs_policy_restore_context(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::restore_context\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = _cfs_get_virtual_resource(&_cfs_virtual_valid_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting cfs_policy::restore_context -- error virtual not found\n");
        return 1;
    }

    struct optEludeList * physicalResource = _cfs_get_physical_resource(&_cfs_physical_free_list, event->physical_id);

    if (!physicalResource) {
        trace("TRACE: exiting cfs_policy::restore_context -- error physical not found\n");
        return 0;
    }

    _cfs_put_virtual_on_physical(virtualResource, physicalResource);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, RESTORE_CONTEXT, start, end);

    trace("TRACE: exiting cfs_policy::restore_context\n");
    return 0;
}

static inline int cfs_policy_on_yield(struct sOSEvent *event) {
    trace("TRACE: entering cfs_policy::on_yield\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = _cfs_get_virtual_resource(&_cfs_virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting cfs_policy::on_yield -- error virtual not found\n");
        return 1;
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        _cfs_put_virtual_off_physical(virtualResource, physicalResource);
    } else {
        _cfs_move_to_valid_queue_safe(virtualResource, &_cfs_virtualOnResourceQueueLock);
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->utilisation += (event->utilization * inverted_weights[virtualResource->resource->priority]) >> 22;


    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_YIELD, start, end);
    trace("TRACE: exiting cfs_policy::on_yield\n");
    return 0;
}

static inline int cfs_policy_on_ready(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::on_ready\n");


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = _cfs_get_virtual_resource(&_cfs_virtual_invalid_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting cfs_policy::on_ready -- error\n");
        return 1;
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->physical_resource = NULL;
    virtualResource->resource->utilisation += (event->utilization * inverted_weights[virtualResource->resource->priority]) >> 22;

    _cfs_move_to_valid_queue_safe(virtualResource, &_cfs_virtualInvalidQueueLock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_READY, start, end);

    trace("TRACE: exiting cfs_policy::on_ready\n");
    return 0;
}

static inline int cfs_policy_on_invalid(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::on_invalid\n");


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    bool on_resource = true;
    struct optVirtualResourceList* virtualResource = _cfs_get_virtual_resource(&_cfs_virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        on_resource = false;
        virtualResource = _cfs_get_virtual_resource(&_cfs_virtual_valid_queue, event->virtual_id);
        if(!virtualResource) {
            trace("TRACE: exiting cfs_policy::on_ready -- error\n");
            return 1;
        }
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        _cfs_put_virtual_off_physical(virtualResource, physicalResource);
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->utilisation += (event->utilization * inverted_weights[virtualResource->resource->priority]) >> 22;

    _cfs_move_list_safe(&(virtualResource->iulist), &_cfs_virtual_invalid_queue, &_cfs_virtualValidQueueLock, &_cfs_virtualInvalidQueueLock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_INVALID, start, end);
    trace("TRACE: exiting cfs_policy::on_invalid\n");
    return 0;
}

static inline int cfs_policy_on_hints(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::on_hints\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_HINTS, start, end);
    trace("TRACE: exiting cfs_policy::on_hints\n");
    return 0;
}

static inline int cfs_policy_on_protection_violation(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::on_protection_violation\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_PROTECTION_VIOLATION, start, end);
    trace("TRACE: exiting cfs_policy::on_protection_violation\n");
    return 0;
}

static inline int cfs_policy_on_create_thread(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::on_create_thread\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = (struct optVirtualResourceList*) malloc(sizeof (struct optVirtualResourceList));

    INIT_LIST_HEAD(&(virtualResource->iulist));

    virtualResource->resource = (struct virtual_resource*) malloc(sizeof (struct virtual_resource));

    virtualResource->resource->physical_resource = NULL;
    virtualResource->resource->virtualId = event->virtual_id;
    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->process = event->attached_process;
    virtualResource->resource->utilisation = 0;
    virtualResource->resource->priority = 0;

    pthread_mutex_lock(&_cfs_virtualInvalidQueueLock);
    list_add_tail(&(virtualResource->iulist), &_cfs_virtual_invalid_queue);
    pthread_mutex_unlock(&_cfs_virtualInvalidQueueLock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_CREATE_THREAD, start, end);
    trace("TRACE: exiting cfs_policy::on_create_thread\n");
    return 0;
}

static inline int cfs_policy_on_dead_thread(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::on_dead_thread\n");


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = _cfs_get_virtual_resource(&_cfs_virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        virtualResource = _cfs_get_virtual_resource(&_cfs_virtual_valid_queue, event->virtual_id);
        if (!virtualResource) {
            virtualResource = _cfs_get_virtual_resource(&_cfs_virtual_invalid_queue, event->virtual_id);
            if (!virtualResource) {
                trace("TRACE: exiting cfs_policy::on_dead_thread -- error\n");
                return 1;
            } else {
                pthread_mutex_lock(&_cfs_virtualInvalidQueueLock);
                list_del(&(virtualResource->iulist));
                pthread_mutex_unlock(&_cfs_virtualInvalidQueueLock);
            }
        } else {
            pthread_mutex_lock(&_cfs_virtualValidQueueLock);
            list_del(&(virtualResource->iulist));
            pthread_mutex_unlock(&_cfs_virtualValidQueueLock);
        }
    } else {
        pthread_mutex_lock(&_cfs_virtualOnResourceQueueLock);
        list_del(&(virtualResource->iulist));
        pthread_mutex_unlock(&_cfs_virtualOnResourceQueueLock);
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        pthread_mutex_lock(&physicalResource->resource->lock);
        physicalResource->resource->virtualResource = NULL;
        pthread_mutex_unlock(&physicalResource->resource->lock);

        _cfs_move_list_safe(&physicalResource->iulist, &_cfs_physical_free_list, &_cfs_physicalUsedListLock, &_cfs_physicalFreeListLock);
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->physical_resource = NULL;

    free(virtualResource->resource);
    free(virtualResource);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_DEAD_THREAD, start, end);
    trace("TRACE: exiting cfs_policy::on_dead_thread\n");
    return 0;
}

static inline int cfs_policy_on_sleep_state_change(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::on_sleep_state_change\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_SLEEP_STATE_CHANGE, start, end);
    trace("TRACE: exiting cfs_policy::on_sleep_state_change\n");
    return 0;
}

static inline int cfs_policy_on_signal(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::on_signal\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_SIGNAL, start, end);
    trace("TRACE: exiting cfs_policy::on_signal\n");
    return 0;
}

static inline int cfs_policy_init(unsigned long numberOfResource) {
    trace("TRACE: entering cfs_policy::init\n");

    init_statistics();
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    _cfs_nb_resources = numberOfResource;
    pthread_mutex_init(&_cfs_resourceListLock, NULL);
    pthread_mutex_init(&_cfs_virtualValidQueueLock, NULL);
    pthread_mutex_init(&_cfs_virtualInvalidQueueLock, NULL);
    pthread_mutex_init(&_cfs_physicalFreeListLock, NULL);
    pthread_mutex_init(&_cfs_physicalUsedListLock, NULL);

    for (unsigned long physical_id = 0; physical_id < _cfs_nb_resources; physical_id++) {
        struct optEludeList * resource = (struct optEludeList*)malloc(sizeof(struct optEludeList));

        INIT_LIST_HEAD(&(resource->iulist));

        resource->resource = &(resourceList[physical_id]);

        list_add_tail(&(resource->iulist), &_cfs_physical_free_list);
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(NULL, INIT, start, end);
    trace("TRACE: exiting cfs_policy::init\n");
    return 0;
}


void cfs_policy_exit() {
    trace("TRACE: entering cfs_policy::exit\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct list_head *listIter, *listIter_s;

    list_for_each_safe(listIter, listIter_s, &_cfs_virtual_on_ressource_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_cfs_virtual_valid_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_cfs_virtual_invalid_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_cfs_physical_free_list) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_cfs_physical_used_list) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node);
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(NULL, EXIT, start, end);

    trace("TRACE: exiting cfs_policy::exit\n");
}