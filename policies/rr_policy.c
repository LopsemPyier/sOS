//
// Created by nathan on 19/06/24.
//

#include "rr_policy.h"

LIST_HEAD(_rr_virtual_on_ressource_queue);
LIST_HEAD(_rr_virtual_valid_queue);
LIST_HEAD(_rr_virtual_invalid_queue);

LIST_HEAD(_rr_physical_free_list);
LIST_HEAD(_rr_physical_used_list);

pthread_mutex_t _rr_resourceListLock;
pthread_mutex_t _rr_virtualOnResourceQueueLock;
pthread_mutex_t _rr_virtualValidQueueLock;
pthread_mutex_t _rr_virtualInvalidQueueLock;
pthread_mutex_t _rr_physicalUsedListLock;
pthread_mutex_t _rr_physicalFreeListLock;
int _rr_nb_resources;

int preemption_time = 2000;




static inline int rr_policy_select_phys_to_virtual(struct sOSEvent *event);
static inline int rr_policy_select_virtual_to_evict(struct sOSEvent *event); // TODO:
static inline int rr_policy_select_virtual_to_load(struct sOSEvent *event);
static inline int rr_policy_save_context(struct sOSEvent *event); // TODO:
static inline int rr_policy_restore_context(struct sOSEvent *event); // TODO:
static inline int rr_policy_on_yield(struct sOSEvent *event);
static inline int rr_policy_on_ready(struct sOSEvent *event);
static inline int rr_policy_on_invalid(struct sOSEvent *event);
static inline int rr_policy_on_hints(struct sOSEvent *event); // TODO:
static inline int rr_policy_on_protection_violation(struct sOSEvent *event); // TODO:
static inline int rr_policy_on_create_thread(struct sOSEvent *event);
static inline int rr_policy_on_dead_thread(struct sOSEvent *event);
static inline int rr_policy_on_sleep_state_change(struct sOSEvent *event); // TODO:
static inline int rr_policy_on_signal(struct sOSEvent *event); // TODO:
static inline int rr_policy_init(unsigned long numberOfResource);
static inline void rr_policy_exit();

struct policy_function rr_policy_functions = {
        .select_phys_to_virtual = &rr_policy_select_phys_to_virtual,
        .select_virtual_to_evict = &rr_policy_select_virtual_to_evict,
        .select_virtual_to_load = &rr_policy_select_virtual_to_load,
        .save_context = &rr_policy_save_context,
        .restore_context = &rr_policy_restore_context,

        .on_yield = &rr_policy_on_yield,
        .on_ready = &rr_policy_on_ready,
        .on_invalid = &rr_policy_on_invalid,
        .on_hints = &rr_policy_on_hints,
        .on_protection_violation = &rr_policy_on_protection_violation,
        .on_create_thread = &rr_policy_on_create_thread,
        .on_dead_thread = &rr_policy_on_dead_thread,
        .on_sleep_state_change = &rr_policy_on_sleep_state_change,
        .on_signal = &rr_policy_on_signal,

        .init = &rr_policy_init,
        .exit = &rr_policy_exit,
};

struct policy_detail rr_policy_detail = {
        .name = "rrPolicy",
        .functions = &rr_policy_functions,
        .is_default = true
};

struct optVirtualResourceList* _rr_get_virtual_resource(struct list_head* list, unsigned long virtualId) {
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

struct optEludeList* _rr_get_physical_resource(struct list_head* list, unsigned long physicalId) {
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

void _rr_move_list_safe(struct list_head * item, struct list_head * dest, pthread_mutex_t* lock1, pthread_mutex_t* lock2) {
    LIST_HEAD(tmp);

    pthread_mutex_lock(lock1);
    list_move_tail(item, &tmp);
    pthread_mutex_unlock(lock1);

    pthread_mutex_lock(lock2);
    list_move_tail(item, dest);
    pthread_mutex_unlock(lock2);
}

void _rr_put_virtual_on_physical(struct optVirtualResourceList* virtual, struct optEludeList* physical) {
    _rr_move_list_safe(&virtual->iulist, &_rr_virtual_on_ressource_queue, &_rr_virtualValidQueueLock, &_rr_virtualOnResourceQueueLock);
    _rr_move_list_safe(&physical->iulist, &_rr_physical_used_list, &_rr_physicalFreeListLock, &_rr_physicalUsedListLock);

    pthread_mutex_lock(&physical->resource->lock);
    physical->resource->virtualResource = virtual;
    pthread_mutex_unlock(&physical->resource->lock);
    virtual->resource->physical_resource = physical;
    clock_gettime(CLOCK_MONOTONIC_RAW, &virtual->resource->last_start);
}

void _rr_put_virtual_off_physical(struct optVirtualResourceList* virtual, struct optEludeList* physical) {
    _rr_move_list_safe(&virtual->iulist, &_rr_virtual_valid_queue, &_rr_virtualOnResourceQueueLock, &_rr_virtualValidQueueLock);
    _rr_move_list_safe(&physical->iulist, &_rr_physical_free_list, &_rr_physicalUsedListLock, &_rr_physicalFreeListLock);

    pthread_mutex_lock(&physical->resource->lock);
    physical->resource->virtualResource = NULL;
    pthread_mutex_unlock(&physical->resource->lock);
    virtual->resource->physical_resource = NULL;
}

void display_physical_list(struct list_head* list) {
    struct optEludeList* phys;
    list_for_each_entry(phys, list, iulist) {
        printf("%lu ", phys->resource->physicalId);
    }
}

void display_virtual_list(struct list_head* list) {
    struct optVirtualResourceList* node;
    list_for_each_entry(node, list, iulist) {
        printf("%lu ", node->resource->virtualId);
    }
}


static inline int rr_policy_select_phys_to_virtual(struct sOSEvent *event) {
    trace("TRACE: entering rr_policy::select_phys_to_virt\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = _rr_get_virtual_resource(&_rr_virtual_valid_queue, event->virtual_id);
    if (!virtualResource) {
        printf("error virtual %lu not found\n", event->virtual_id);
trace("TRACE: exiting rr_policy::select_phys_to_virt\n");
        return 1;
    }

    if (list_empty(&_rr_physical_free_list)) {
        trace("TRACE: exiting rr_policy::select_phys_to_virt -- error no free physical\n");
        return 1;
    }

    printf("The free list is : ");
    display_physical_list(&_rr_physical_free_list);
    printf("\n");


    struct optEludeList * phys = list_first_entry(&_rr_physical_free_list, struct optEludeList, iulist);

    _rr_put_virtual_on_physical(virtualResource, phys);

    event->physical_id = phys->resource->physicalId;
    unsigned long tmp = virtualResource->resource->last_event_id;
    virtualResource->resource->last_event_id = event->event_id;

    event->event_id = tmp;

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SELECT_PHYS_TO_VIRTUAL, start, end);

    trace("TRACE: exiting rr_policy::select_phys_to_virt\n");
    return 0;
}

static inline int rr_policy_select_virtual_to_evict(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::select_virtual_to_evict\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SELECT_VIRTUAL_TO_EVICT, start, end);

    trace("TRACE: exiting rr_policy::select_virtual_to_evict\n");
    return 0;
}

static inline int rr_policy_select_virtual_to_load(struct sOSEvent* event) {
    // trace("TRACE: entering rr_policy::select_virtual_to_load\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    if (list_empty(&_rr_virtual_valid_queue)) {
        // trace("TRACE: exiting rr_policy::select_virtual_to_load -- empty\n");
        return 1;
    }

    struct optEludeList * physicalResource = _rr_get_physical_resource(&_rr_physical_free_list, event->physical_id);

    if (!physicalResource) {
        physicalResource = _rr_get_physical_resource(&_rr_physical_used_list, event->physical_id);

        if (!physicalResource) {
            printf("error physical %lu not found\n", event->physical_id);
trace("TRACE: exiting rr_policy::select_virtual_to_load\n");
            return 0;
        }
    }

    if (physicalResource->resource->virtualResource && !(start.tv_sec - physicalResource->resource->virtualResource->resource->last_start.tv_sec) * 1000000 + start.tv_nsec - physicalResource->resource->virtualResource->resource->last_start.tv_nsec > preemption_time) {
        return 1;
    }

    printf("The valid queue is : ");
    display_virtual_list(&_rr_virtual_valid_queue);
    printf("\n");

    printf("The on_resource queue is : ");
    display_virtual_list(&_rr_virtual_on_ressource_queue);
    printf("\n");

    printf("The invalid queue is : ");
    display_virtual_list(&_rr_virtual_invalid_queue);
    printf("\n");

    printf("The free list is : ");
    display_physical_list(&_rr_physical_free_list);
    printf("\n");

    printf("The used list is : ");
    display_physical_list(&_rr_physical_used_list);
    printf("\n");

    struct optVirtualResourceList* virtualResource = list_first_entry(&_rr_virtual_valid_queue, struct optVirtualResourceList, iulist);

    _rr_put_virtual_on_physical(virtualResource, physicalResource);

    event->virtual_id = virtualResource->resource->virtualId;
    event->event_id = virtualResource->resource->last_event_id;

    printf("Selected task %lu to assigned to cpu %lu\n", event->virtual_id, event->physical_id);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SELECT_VIRTUAL_TO_LOAD, start, end);
    trace("TRACE: exiting rr_policy::select_virtual_to_load\n");
    return 0;
}

static inline int rr_policy_save_context(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::save_context\n");
    printf("virtual %lu physical %lu\n", event->virtual_id, event->physical_id);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = _rr_get_virtual_resource(&_rr_virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        printf("error virtual %lu not found\n", event->virtual_id);
trace("TRACE: exiting rr_policy::save_context\n");
        return 1;
    }

    struct optEludeList * physicalResource = _rr_get_physical_resource(&_rr_physical_used_list, event->physical_id);

    if (!physicalResource) {
        printf("error physical %lu not found\n", event->physical_id);
trace("TRACE: exiting rr_policy::save_context\n");
        return 0;
    }

    _rr_put_virtual_off_physical(virtualResource, physicalResource);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SAVE_CONTEXT, start, end);

    trace("TRACE: exiting rr_policy::save_context\n");
    return 0;
}

static inline int rr_policy_restore_context(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::restore_context\n");
    printf("virtual %lu physical %lu\n", event->virtual_id, event->physical_id);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = _rr_get_virtual_resource(&_rr_virtual_valid_queue, event->virtual_id);
    if (!virtualResource) {
        printf("error virtual %lu not found\n", event->virtual_id);
trace("TRACE: exiting rr_policy::restore_context\n");
        return 1;
    }

    struct optEludeList * physicalResource = _rr_get_physical_resource(&_rr_physical_free_list, event->physical_id);

    if (!physicalResource) {
        printf("error physical %lu not found\n", event->physical_id);
trace("TRACE: exiting rr_policy::restore_context\n");
        return 0;
    }

    _rr_put_virtual_on_physical(virtualResource, physicalResource);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, RESTORE_CONTEXT, start, end);

    trace("TRACE: exiting rr_policy::restore_context\n");
    return 0;
}

static inline int rr_policy_on_yield(struct sOSEvent *event) {
    trace("TRACE: entering rr_policy::on_yield\n");
printf("virtual %lu\n", event->virtual_id);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = _rr_get_virtual_resource(&_rr_virtual_on_ressource_queue, event->virtual_id);
    bool preempted = false;
    if (!virtualResource) {
        preempted = true;
        virtualResource = _rr_get_virtual_resource(&_rr_virtual_valid_queue, event->virtual_id);
        if (!virtualResource) {
            printf("error virtual %lu not found\n", event->virtual_id);
            trace("TRACE: exiting rr_policy::on_yield\n");
            return 1;
        }
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        _rr_put_virtual_off_physical(virtualResource, physicalResource);
    } else if (!preempted) {
        _rr_move_list_safe(&(virtualResource->iulist), &_rr_virtual_valid_queue, &_rr_virtualOnResourceQueueLock, &_rr_virtualValidQueueLock);
    }

    virtualResource->resource->last_event_id = event->event_id;


    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_YIELD, start, end);
    trace("TRACE: exiting rr_policy::on_yield\n");
    return 0;
}

static inline int rr_policy_on_ready(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::on_ready\n");
printf("virtual %lu\n", event->virtual_id);


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = _rr_get_virtual_resource(&_rr_virtual_invalid_queue, event->virtual_id);
    if (!virtualResource) {
        printf("error virtual %lu not found\n", event->virtual_id);
trace("TRACE: exiting rr_policy::on_ready\n");
        return 1;
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->physical_resource = NULL;

    _rr_move_list_safe(&(virtualResource->iulist), &_rr_virtual_valid_queue, &_rr_virtualInvalidQueueLock, &_rr_virtualValidQueueLock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_READY, start, end);

    trace("TRACE: exiting rr_policy::on_ready\n");
    return 0;
}

static inline int rr_policy_on_invalid(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::on_invalid\n");
printf("virtual %lu\n", event->virtual_id);


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = _rr_get_virtual_resource(&_rr_virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        virtualResource = _rr_get_virtual_resource(&_rr_virtual_valid_queue, event->virtual_id);
        if(!virtualResource) {
            printf("error virtual %lu not found\n", event->virtual_id);
trace("TRACE: exiting rr_policy::on_invalid\n");
            return 1;
        }
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        _rr_put_virtual_off_physical(virtualResource, physicalResource);
    }

    virtualResource->resource->last_event_id = event->event_id;

    _rr_move_list_safe(&(virtualResource->iulist), &_rr_virtual_invalid_queue, &_rr_virtualValidQueueLock, &_rr_virtualInvalidQueueLock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_INVALID, start, end);
    trace("TRACE: exiting rr_policy::on_invalid\n");
    return 0;
}

static inline int rr_policy_on_hints(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::on_hints\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_HINTS, start, end);
    trace("TRACE: exiting rr_policy::on_hints\n");
    return 0;
}

static inline int rr_policy_on_protection_violation(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::on_protection_violation\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_PROTECTION_VIOLATION, start, end);
    trace("TRACE: exiting rr_policy::on_protection_violation\n");
    return 0;
}

static inline int rr_policy_on_create_thread(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::on_create_thread\n");

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
    clock_gettime(CLOCK_MONOTONIC_RAW, &virtualResource->resource->last_start);

    pthread_mutex_lock(&_rr_virtualInvalidQueueLock);
    list_add_tail(&(virtualResource->iulist), &_rr_virtual_invalid_queue);
    pthread_mutex_unlock(&_rr_virtualInvalidQueueLock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_CREATE_THREAD, start, end);
    trace("TRACE: exiting rr_policy::on_create_thread\n");
    return 0;
}

static inline int rr_policy_on_dead_thread(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::on_dead_thread\n");


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = _rr_get_virtual_resource(&_rr_virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        virtualResource = _rr_get_virtual_resource(&_rr_virtual_valid_queue, event->virtual_id);
        if (!virtualResource) {
            virtualResource = _rr_get_virtual_resource(&_rr_virtual_invalid_queue, event->virtual_id);
            if (!virtualResource) {
                printf("error virtual %lu not found\n", event->virtual_id);
trace("TRACE: exiting rr_policy::on_dead_thread\n");
                return 1;
            } else {
                pthread_mutex_lock(&_rr_virtualInvalidQueueLock);
                list_del(&(virtualResource->iulist));
                pthread_mutex_unlock(&_rr_virtualInvalidQueueLock);
            }
        } else {
            pthread_mutex_lock(&_rr_virtualValidQueueLock);
            list_del(&(virtualResource->iulist));
            pthread_mutex_unlock(&_rr_virtualValidQueueLock);
        }
    } else {
        pthread_mutex_lock(&_rr_virtualOnResourceQueueLock);
        list_del(&(virtualResource->iulist));
        pthread_mutex_unlock(&_rr_virtualOnResourceQueueLock);
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        pthread_mutex_lock(&physicalResource->resource->lock);
        physicalResource->resource->virtualResource = NULL;
        pthread_mutex_unlock(&physicalResource->resource->lock);

        _rr_move_list_safe(&physicalResource->iulist, &_rr_physical_free_list, &_rr_physicalUsedListLock, &_rr_physicalFreeListLock);
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->physical_resource = NULL;

    free(virtualResource->resource);
    free(virtualResource);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_DEAD_THREAD, start, end);
    trace("TRACE: exiting rr_policy::on_dead_thread\n");
    return 0;
}

static inline int rr_policy_on_sleep_state_change(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::on_sleep_state_change\n");
printf("physical %lu\n", event->physical_id);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    if (event->sleep == BUSY) {
        struct optEludeList * physicalResource = _rr_get_physical_resource(&_rr_physical_free_list, event->physical_id);
        bool move = true;
        if (!physicalResource) {
            move = false;
            physicalResource = _rr_get_physical_resource(&_rr_physical_used_list, event->physical_id);
            if (!physicalResource) {
                printf("error physical %lu not found\n", event->physical_id);
trace("TRACE: exiting rr_policy::on_sleep_state_change\n");
                return 1;
            }
        }

        if (physicalResource->resource->virtualResource) {
            event->virtual_id = physicalResource->resource->virtualResource->resource->virtualId;
            rr_policy_save_context(event);
            move = true;
        }

        if (move)
            _rr_move_list_safe(&physicalResource->iulist, &_rr_physical_used_list, &_rr_physicalFreeListLock, &_rr_physicalUsedListLock);


    } else if (event->sleep == AVAILABLE) {
        struct optEludeList * physicalResource = _rr_get_physical_resource(&_rr_physical_used_list, event->physical_id);
        if (!physicalResource) {
            printf("Error physical %lu not found\n", event->physical_id);
            trace("TRACE: exiting rr_policy::on_sleep_state_change\n");
            return 1;
        }

        _rr_move_list_safe(&physicalResource->iulist, &_rr_physical_free_list, &_rr_physicalUsedListLock, &_rr_physicalFreeListLock);
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_SLEEP_STATE_CHANGE, start, end);
    trace("TRACE: exiting rr_policy::on_sleep_state_change\n");
    return 0;
}

static inline int rr_policy_on_signal(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::on_signal\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_SIGNAL, start, end);
    trace("TRACE: exiting rr_policy::on_signal\n");
    return 0;
}

static inline int rr_policy_init(unsigned long numberOfResource) {
    trace("TRACE: entering rr_policy::init\n");

    init_statistics();
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    _rr_nb_resources = numberOfResource;
    pthread_mutex_init(&_rr_resourceListLock, NULL);
    pthread_mutex_init(&_rr_virtualValidQueueLock, NULL);
    pthread_mutex_init(&_rr_virtualInvalidQueueLock, NULL);
    pthread_mutex_init(&_rr_physicalFreeListLock, NULL);
    pthread_mutex_init(&_rr_physicalUsedListLock, NULL);

    for (unsigned long physical_id = 0; physical_id < _rr_nb_resources; physical_id++) {
        struct optEludeList * resource = (struct optEludeList*)malloc(sizeof(struct optEludeList));

        INIT_LIST_HEAD(&(resource->iulist));

        resource->resource = &(resourceList[physical_id]);

        list_add_tail(&(resource->iulist), &_rr_physical_free_list);
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(NULL, INIT, start, end);
    trace("TRACE: exiting rr_policy::init\n");
    return 0;
}


void rr_policy_exit() {
    trace("TRACE: entering rr_policy::exit\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct list_head *listIter, *listIter_s;

    list_for_each_safe(listIter, listIter_s, &_rr_virtual_on_ressource_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_rr_virtual_valid_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_rr_virtual_invalid_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_rr_physical_free_list) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &_rr_physical_used_list) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node);
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(NULL, EXIT, start, end);

    trace("TRACE: exiting rr_policy::exit\n");
}