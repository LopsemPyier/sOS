//
// Created by nathan on 19/06/24.
//

#include "fifo_policy.h"

LIST_HEAD(_fifo_virtual_on_ressource_queue);
LIST_HEAD(_fifo_virtual_valid_queue);
LIST_HEAD(_fifo_virtual_invalid_queue);

LIST_HEAD(_fifo_physical_free_list);
LIST_HEAD(_fifo_physical_used_list);

pthread_mutex_t _fifo_resourceListLock;
pthread_mutex_t _fifo_virtualOnResourceQueueLock;
pthread_mutex_t _fifo_virtualValidQueueLock;
pthread_mutex_t _fifo_virtualInvalidQueueLock;
pthread_mutex_t _fifo_physicalUsedListLock;
pthread_mutex_t _fifo_physicalFreeListLock;
int _fifo_nb_resources;




static inline int fifo_policy_select_phys_to_virtual(struct sOSEvent *event);
static inline int fifo_policy_select_virtual_to_evict(struct sOSEvent *event); // TODO:
static inline int fifo_policy_select_virtual_to_load(struct sOSEvent *event);
static inline int fifo_policy_save_context(struct sOSEvent *event); // TODO:
static inline int fifo_policy_restore_context(struct sOSEvent *event); // TODO:
static inline int fifo_policy_on_yield(struct sOSEvent *event);
static inline int fifo_policy_on_ready(struct sOSEvent *event);
static inline int fifo_policy_on_invalid(struct sOSEvent *event);
static inline int fifo_policy_on_hints(struct sOSEvent *event); // TODO:
static inline int fifo_policy_on_protection_violation(struct sOSEvent *event); // TODO:
static inline int fifo_policy_on_create_thread(struct sOSEvent *event);
static inline int fifo_policy_on_dead_thread(struct sOSEvent *event);
static inline int fifo_policy_on_sleep_state_change(struct sOSEvent *event); // TODO:
static inline int fifo_policy_on_signal(struct sOSEvent *event); // TODO:
static inline int fifo_policy_init(unsigned long numberOfResource);
static inline void fifo_policy_exit();

struct policy_function fifo_policy_functions = {
        .select_phys_to_virtual = &fifo_policy_select_phys_to_virtual,
        .select_virtual_to_evict = &fifo_policy_select_virtual_to_evict,
        .select_virtual_to_load = &fifo_policy_select_virtual_to_load,
        .save_context = &fifo_policy_save_context,
        .restore_context = &fifo_policy_restore_context,

        .on_yield = &fifo_policy_on_yield,
        .on_ready = &fifo_policy_on_ready,
        .on_invalid = &fifo_policy_on_invalid,
        .on_hints = &fifo_policy_on_hints,
        .on_protection_violation = &fifo_policy_on_protection_violation,
        .on_create_thread = &fifo_policy_on_create_thread,
        .on_dead_thread = &fifo_policy_on_dead_thread,
        .on_sleep_state_change = &fifo_policy_on_sleep_state_change,
        .on_signal = &fifo_policy_on_signal,

        .init = &fifo_policy_init,
        .exit = &fifo_policy_exit,
};

struct policy_detail fifo_policy_detail = {
        .name = "fifoPolicy",
        .functions = &fifo_policy_functions,
        .is_default = false
};

struct optVirtualResourceList* _fifo_get_virtual_resource(struct list_head* list, unsigned long virtualId) {
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

struct optEludeList* _fifo_get_physical_resource(struct list_head* list, unsigned long physicalId) {
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

void _fifo_move_list_safe(struct list_head * item, struct list_head * dest, pthread_mutex_t* lock1, pthread_mutex_t* lock2) {
    LIST_HEAD(tmp);

    pthread_mutex_lock(lock1);
    list_move_tail(item, &tmp);
    pthread_mutex_unlock(lock1);

    pthread_mutex_lock(lock2);
    list_move_tail(item, dest);
    pthread_mutex_unlock(lock2);
}

void _fifo_put_virtual_on_physical(struct optVirtualResourceList* virtual, struct optEludeList* physical) {
    _fifo_move_list_safe(&virtual->iulist, &_fifo_virtual_on_ressource_queue, &_fifo_virtualValidQueueLock, &_fifo_virtualOnResourceQueueLock);
    _fifo_move_list_safe(&physical->iulist, &_fifo_physical_used_list, &_fifo_physicalFreeListLock, &_fifo_physicalUsedListLock);

    pthread_mutex_lock(&physical->resource->lock);
    physical->resource->virtualResource = virtual;
    pthread_mutex_unlock(&physical->resource->lock);
    virtual->resource->physical_resource = physical;
}

void _fifo_put_virtual_off_physical(struct optVirtualResourceList* virtual, struct optEludeList* physical) {
    _fifo_move_list_safe(&virtual->iulist, &_fifo_virtual_valid_queue, &_fifo_virtualOnResourceQueueLock, &_fifo_virtualValidQueueLock);
    _fifo_move_list_safe(&physical->iulist, &_fifo_physical_free_list, &_fifo_physicalUsedListLock, &_fifo_physicalFreeListLock);

    pthread_mutex_lock(&physical->resource->lock);
    physical->resource->virtualResource = NULL;
    pthread_mutex_unlock(&physical->resource->lock);
    virtual->resource->physical_resource = NULL;
}


static inline int fifo_policy_select_phys_to_virtual(struct sOSEvent *event) {
    trace("TRACE: entering fifo_policy::select_phys_to_virt\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = _fifo_get_virtual_resource(&_fifo_virtual_valid_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting fifo_policy::select_phys_to_virt -- error virtual not found\n");
        return 1;
    }

    if (list_empty(&_fifo_physical_free_list)) {
        trace("TRACE: exiting fifo_policy::select_phys_to_virt -- error no free physical\n");
        return 1;
    }


    struct optEludeList * phys = list_first_entry(&_fifo_physical_free_list, struct optEludeList, iulist);

    _fifo_put_virtual_on_physical(virtualResource, phys);

    event->physical_id = phys->resource->physicalId;
    unsigned long tmp = virtualResource->resource->last_event_id;
    virtualResource->resource->last_event_id = event->event_id;

    event->event_id = tmp;

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SELECT_PHYS_TO_VIRTUAL, start, end);

    trace("TRACE: exiting fifo_policy::select_phys_to_virt\n");
    return 0;
}

static inline int fifo_policy_select_virtual_to_evict(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::select_virtual_to_evict\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SELECT_VIRTUAL_TO_EVICT, start, end);

    trace("TRACE: exiting fifo_policy::select_virtual_to_evict\n");
    return 0;
}

static inline int fifo_policy_select_virtual_to_load(struct sOSEvent* event) {
    // trace("TRACE: entering fifo_policy::select_virtual_to_load\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optEludeList * physicalResource = _fifo_get_physical_resource(&_fifo_physical_used_list, event->physical_id);

    if (physicalResource && physicalResource->resource->virtualResource) {
//        trace("TRACE: exiting fifo_policy::select_virtual_to_load -- already in use\n");
        return 1;
    }

    if (list_empty(&_fifo_virtual_valid_queue)) {
        // trace("TRACE: exiting fifo_policy::select_virtual_to_load -- empty\n");
        return 1;
    }

    struct optVirtualResourceList* virtualResource = list_first_entry(&_fifo_virtual_valid_queue, struct optVirtualResourceList, iulist);

    physicalResource = _fifo_get_physical_resource(&_fifo_physical_free_list, event->physical_id);

    if (!physicalResource) {
        trace("TRACE: exiting fifo_policy::select_virtual_to_load -- error\n");
        return 0;
    }

    _fifo_put_virtual_on_physical(virtualResource, physicalResource);

    event->virtual_id = virtualResource->resource->virtualId;
    event->event_id = virtualResource->resource->last_event_id;

    printf("Selected task %lu to assigned to cpu %lu\n", event->virtual_id, event->physical_id);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SELECT_VIRTUAL_TO_LOAD, start, end);
    trace("TRACE: exiting fifo_policy::select_virtual_to_load\n");
    return 0;
}

static inline int fifo_policy_save_context(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::save_context\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = _fifo_get_virtual_resource(&_fifo_virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting fifo_policy::save_context -- error virtual not found\n");
        return 1;
    }

    struct optEludeList * physicalResource = _fifo_get_physical_resource(&_fifo_physical_used_list, event->physical_id);

    if (!physicalResource) {
        trace("TRACE: exiting fifo_policy::save_context -- error physical not found\n");
        return 0;
    }

    _fifo_put_virtual_off_physical(virtualResource, physicalResource);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SAVE_CONTEXT, start, end);

    trace("TRACE: exiting fifo_policy::save_context\n");
    return 0;
}

static inline int fifo_policy_restore_context(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::restore_context\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = _fifo_get_virtual_resource(&_fifo_virtual_valid_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting fifo_policy::restore_context -- error virtual not found\n");
        return 1;
    }

    struct optEludeList * physicalResource = _fifo_get_physical_resource(&_fifo_physical_free_list, event->physical_id);

    if (!physicalResource) {
        trace("TRACE: exiting fifo_policy::restore_context -- error physical not found\n");
        return 0;
    }

    _fifo_put_virtual_on_physical(virtualResource, physicalResource);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, RESTORE_CONTEXT, start, end);

    trace("TRACE: exiting fifo_policy::restore_context\n");
    return 0;
}

static inline int fifo_policy_on_yield(struct sOSEvent *event) {
    trace("TRACE: entering fifo_policy::on_yield\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = _fifo_get_virtual_resource(&_fifo_virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting fifo_policy::on_yield -- error virtual not found\n");
        return 1;
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        _fifo_put_virtual_off_physical(virtualResource, physicalResource);
    } else {
        _fifo_move_list_safe(&(virtualResource->iulist), &_fifo_virtual_valid_queue, &_fifo_virtualOnResourceQueueLock, &_fifo_virtualValidQueueLock);
    }

    virtualResource->resource->last_event_id = event->event_id;


    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_YIELD, start, end);
    trace("TRACE: exiting fifo_policy::on_yield\n");
    return 0;
}

static inline int fifo_policy_on_ready(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_ready\n");


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = _fifo_get_virtual_resource(&_fifo_virtual_invalid_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting fifo_policy::on_ready -- error\n");
        return 1;
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->physical_resource = NULL;

    _fifo_move_list_safe(&(virtualResource->iulist), &_fifo_virtual_valid_queue, &_fifo_virtualInvalidQueueLock, &_fifo_virtualValidQueueLock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_READY, start, end);

    trace("TRACE: exiting fifo_policy::on_ready\n");
    return 0;
}

static inline int fifo_policy_on_invalid(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_invalid\n");


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    bool on_resource = true;
    struct optVirtualResourceList* virtualResource = _fifo_get_virtual_resource(&_fifo_virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        on_resource = false;
        virtualResource = _fifo_get_virtual_resource(&_fifo_virtual_valid_queue, event->virtual_id);
        if(!virtualResource) {
            trace("TRACE: exiting fifo_policy::on_ready -- error\n");
            return 1;
        }
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        _fifo_put_virtual_off_physical(virtualResource, physicalResource);
    }

    virtualResource->resource->last_event_id = event->event_id;

    _fifo_move_list_safe(&(virtualResource->iulist), &_fifo_virtual_invalid_queue, &_fifo_virtualValidQueueLock, &_fifo_virtualInvalidQueueLock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_INVALID, start, end);
    trace("TRACE: exiting fifo_policy::on_invalid\n");
    return 0;
}

static inline int fifo_policy_on_hints(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_hints\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_HINTS, start, end);
    trace("TRACE: exiting fifo_policy::on_hints\n");
    return 0;
}

static inline int fifo_policy_on_protection_violation(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_protection_violation\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_PROTECTION_VIOLATION, start, end);
    trace("TRACE: exiting fifo_policy::on_protection_violation\n");
    return 0;
}

static inline int fifo_policy_on_create_thread(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_create_thread\n");

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

    pthread_mutex_lock(&_fifo_virtualInvalidQueueLock);
    list_add_tail(&(virtualResource->iulist), &_fifo_virtual_invalid_queue);
    pthread_mutex_unlock(&_fifo_virtualInvalidQueueLock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_CREATE_THREAD, start, end);
    trace("TRACE: exiting fifo_policy::on_create_thread\n");
    return 0;
}

static inline int fifo_policy_on_dead_thread(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_dead_thread\n");


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = _fifo_get_virtual_resource(&_fifo_virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        virtualResource = _fifo_get_virtual_resource(&_fifo_virtual_valid_queue, event->virtual_id);
        if (!virtualResource) {
            virtualResource = _fifo_get_virtual_resource(&_fifo_virtual_invalid_queue, event->virtual_id);
            if (!virtualResource) {
                trace("TRACE: exiting fifo_policy::on_dead_thread -- error\n");
                return 1;
            } else {
                pthread_mutex_lock(&_fifo_virtualInvalidQueueLock);
                list_del(&(virtualResource->iulist));
                pthread_mutex_unlock(&_fifo_virtualInvalidQueueLock);
            }
        } else {
            pthread_mutex_lock(&_fifo_virtualValidQueueLock);
            list_del(&(virtualResource->iulist));
            pthread_mutex_unlock(&_fifo_virtualValidQueueLock);
        }
    } else {
        pthread_mutex_lock(&_fifo_virtualOnResourceQueueLock);
        list_del(&(virtualResource->iulist));
        pthread_mutex_unlock(&_fifo_virtualOnResourceQueueLock);
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        pthread_mutex_lock(&physicalResource->resource->lock);
        physicalResource->resource->virtualResource = NULL;
        pthread_mutex_unlock(&physicalResource->resource->lock);

        _fifo_move_list_safe(&physicalResource->iulist, &_fifo_physical_free_list, &_fifo_physicalUsedListLock, &_fifo_physicalFreeListLock);
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->physical_resource = NULL;

    free(virtualResource->resource);
    free(virtualResource);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_DEAD_THREAD, start, end);
    trace("TRACE: exiting fifo_policy::on_dead_thread\n");
    return 0;
}

static inline int fifo_policy_on_sleep_state_change(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_sleep_state_change\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_SLEEP_STATE_CHANGE, start, end);
    trace("TRACE: exiting fifo_policy::on_sleep_state_change\n");
    return 0;
}

static inline int fifo_policy_on_signal(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_signal\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_SIGNAL, start, end);
    trace("TRACE: exiting fifo_policy::on_signal\n");
    return 0;
}

static inline int fifo_policy_init(unsigned long numberOfResource) {
    trace("TRACE: entering fifo_policy::init\n");

    init_statistics();
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    _fifo_nb_resources = numberOfResource;
    pthread_mutex_init(&_fifo_resourceListLock, NULL);
    pthread_mutex_init(&_fifo_virtualValidQueueLock, NULL);
    pthread_mutex_init(&_fifo_virtualInvalidQueueLock, NULL);
    pthread_mutex_init(&_fifo_physicalFreeListLock, NULL);
    pthread_mutex_init(&_fifo_physicalUsedListLock, NULL);

    for (unsigned long physical_id = 0; physical_id < _fifo_nb_resources; physical_id++) {
        struct optEludeList * resource = (struct optEludeList*)malloc(sizeof(struct optEludeList));

        INIT_LIST_HEAD(&(resource->iulist));

        resource->resource = &(resourceList[physical_id]);

        list_add_tail(&(resource->iulist), &_fifo_physical_free_list);
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(NULL, INIT, start, end);
    trace("TRACE: exiting fifo_policy::init\n");
    return 0;
}


void fifo_policy_exit() {
    trace("TRACE: entering fifo_policy::exit\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct list_head *listIter, *listIter_s;

    list_for_each_safe(listIter, listIter_s, &_fifo_virtual_on_ressource_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_fifo_virtual_valid_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_fifo_virtual_invalid_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_fifo_physical_free_list) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_fifo_physical_used_list) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node);
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(NULL, EXIT, start, end);

    trace("TRACE: exiting fifo_policy::exit\n");
}