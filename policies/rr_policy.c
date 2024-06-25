//
// Created by nathan on 19/06/24.
//

#include "rr_policy.h"

LIST_HEAD(virtual_on_ressource_queue);
LIST_HEAD(virtual_valid_queue);
LIST_HEAD(virtual_invalid_queue);

LIST_HEAD(physical_free_list);
LIST_HEAD(physical_used_list);

pthread_mutex_t resourceListLock;
pthread_mutex_t virtualOnResourceQueueLock;
pthread_mutex_t virtualValidQueueLock;
pthread_mutex_t virtualInvalidQueueLock;
pthread_mutex_t physicalUsedListLock;
pthread_mutex_t physicalFreeListLock;
int nb_resources;




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

struct optVirtualResourceList* get_virtual_resource(struct list_head* list, unsigned long virtualId) {
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

struct optEludeList* get_physical_resource(struct list_head* list, unsigned long physicalId) {
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

void move_list_safe(struct list_head * item, struct list_head * dest, pthread_mutex_t* lock1, pthread_mutex_t* lock2) {
    LIST_HEAD(tmp);

    pthread_mutex_lock(lock1);
    list_move_tail(item, &tmp);
    pthread_mutex_unlock(lock1);

    pthread_mutex_lock(lock2);
    list_move_tail(item, dest);
    pthread_mutex_unlock(lock2);
}

void put_virtual_on_physical(struct optVirtualResourceList* virtual, struct optEludeList* physical) {
    move_list_safe(&virtual->iulist, &virtual_on_ressource_queue, &virtualValidQueueLock, &virtualOnResourceQueueLock);
    move_list_safe(&physical->iulist, &physical_used_list, &physicalFreeListLock, &physicalUsedListLock);

    pthread_mutex_lock(&physical->resource->lock);
    physical->resource->virtualResource = virtual;
    pthread_mutex_unlock(&physical->resource->lock);
    virtual->resource->physical_resource = physical;
}

void put_virtual_off_physical(struct optVirtualResourceList* virtual, struct optEludeList* physical) {
    move_list_safe(&virtual->iulist, &virtual_valid_queue, &virtualOnResourceQueueLock, &virtualValidQueueLock);
    move_list_safe(&physical->iulist, &physical_free_list, &physicalUsedListLock, &physicalFreeListLock);

    pthread_mutex_lock(&physical->resource->lock);
    physical->resource->virtualResource = NULL;
    pthread_mutex_unlock(&physical->resource->lock);
    virtual->resource->physical_resource = NULL;
}


static inline int rr_policy_select_phys_to_virtual(struct sOSEvent *event) {
    trace("TRACE: entering rr_policy::select_phys_to_virt\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_valid_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting rr_policy::select_phys_to_virt -- error virtual not found\n");
        return 1;
    }

    if (list_empty(&physical_free_list)) {
        trace("TRACE: exiting rr_policy::select_phys_to_virt -- error no free physical\n");
        return 1;
    }


    struct optEludeList * phys = list_first_entry(&physical_free_list, struct optEludeList, iulist);

    put_virtual_on_physical(virtualResource, phys);

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

    if (list_empty(&virtual_valid_queue)) {
        // trace("TRACE: exiting rr_policy::select_virtual_to_load -- empty\n");
        return 1;
    }

    struct optEludeList * physicalResource = get_physical_resource(&physical_free_list, event->physical_id);

    if (!physicalResource) {
        physicalResource = get_physical_resource(&physical_used_list, event->physical_id);

        if (!physicalResource) {
            trace("TRACE: exiting rr_policy::select_virtual_to_load -- error\n");
            return 0;
        }
    }

    if (physicalResource->resource->virtualResource) {
        event->virtual_id = physicalResource->resource->virtualResource->resource->virtualId;
        rr_policy_save_context(event);
    }

    struct optVirtualResourceList* virtualResource = list_first_entry(&virtual_valid_queue, struct optVirtualResourceList, iulist);

    put_virtual_on_physical(virtualResource, physicalResource);

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

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting rr_policy::save_context -- error virtual not found\n");
        return 1;
    }

    struct optEludeList * physicalResource = get_physical_resource(&physical_used_list, event->physical_id);

    if (!physicalResource) {
        trace("TRACE: exiting rr_policy::save_context -- error physical not found\n");
        return 0;
    }

    put_virtual_off_physical(virtualResource, physicalResource);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SAVE_CONTEXT, start, end);

    trace("TRACE: exiting rr_policy::save_context\n");
    return 0;
}

static inline int rr_policy_restore_context(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::restore_context\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_valid_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting rr_policy::restore_context -- error virtual not found\n");
        return 1;
    }

    struct optEludeList * physicalResource = get_physical_resource(&physical_free_list, event->physical_id);

    if (!physicalResource) {
        trace("TRACE: exiting rr_policy::restore_context -- error physical not found\n");
        return 0;
    }

    put_virtual_on_physical(virtualResource, physicalResource);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, RESTORE_CONTEXT, start, end);

    trace("TRACE: exiting rr_policy::restore_context\n");
    return 0;
}

static inline int rr_policy_on_yield(struct sOSEvent *event) {
    trace("TRACE: entering rr_policy::on_yield\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting rr_policy::on_yield -- error virtual not found\n");
        return 1;
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        put_virtual_off_physical(virtualResource, physicalResource);
    } else {
        move_list_safe(&(virtualResource->iulist), &virtual_valid_queue, &virtualOnResourceQueueLock, &virtualValidQueueLock);
    }

    virtualResource->resource->last_event_id = event->event_id;


    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_YIELD, start, end);
    trace("TRACE: exiting rr_policy::on_yield\n");
    return 0;
}

static inline int rr_policy_on_ready(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::on_ready\n");


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_invalid_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting rr_policy::on_ready -- error\n");
        return 1;
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->physical_resource = NULL;

    move_list_safe(&(virtualResource->iulist), &virtual_valid_queue, &virtualInvalidQueueLock, &virtualValidQueueLock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_READY, start, end);

    trace("TRACE: exiting rr_policy::on_ready\n");
    return 0;
}

static inline int rr_policy_on_invalid(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::on_invalid\n");


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    bool on_resource = true;
    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        on_resource = false;
        virtualResource = get_virtual_resource(&virtual_valid_queue, event->virtual_id);
        if(!virtualResource) {
            trace("TRACE: exiting rr_policy::on_ready -- error\n");
            return 1;
        }
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        put_virtual_off_physical(virtualResource, physicalResource);
    }

    virtualResource->resource->last_event_id = event->event_id;

    move_list_safe(&(virtualResource->iulist), &virtual_invalid_queue, &virtualValidQueueLock, &virtualInvalidQueueLock);

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

    pthread_mutex_lock(&virtualInvalidQueueLock);
    list_add_tail(&(virtualResource->iulist), &virtual_invalid_queue);
    pthread_mutex_unlock(&virtualInvalidQueueLock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_CREATE_THREAD, start, end);
    trace("TRACE: exiting rr_policy::on_create_thread\n");
    return 0;
}

static inline int rr_policy_on_dead_thread(struct sOSEvent* event) {
    trace("TRACE: entering rr_policy::on_dead_thread\n");


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        virtualResource = get_virtual_resource(&virtual_valid_queue, event->virtual_id);
        if (!virtualResource) {
            virtualResource = get_virtual_resource(&virtual_invalid_queue, event->virtual_id);
            if (!virtualResource) {
                trace("TRACE: exiting rr_policy::on_dead_thread -- error\n");
                return 1;
            } else {
                pthread_mutex_lock(&virtualInvalidQueueLock);
                list_del(&(virtualResource->iulist));
                pthread_mutex_unlock(&virtualInvalidQueueLock);
            }
        } else {
            pthread_mutex_lock(&virtualValidQueueLock);
            list_del(&(virtualResource->iulist));
            pthread_mutex_unlock(&virtualValidQueueLock);
        }
    } else {
        pthread_mutex_lock(&virtualOnResourceQueueLock);
        list_del(&(virtualResource->iulist));
        pthread_mutex_unlock(&virtualOnResourceQueueLock);
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        pthread_mutex_lock(&physicalResource->resource->lock);
        physicalResource->resource->virtualResource = NULL;
        pthread_mutex_unlock(&physicalResource->resource->lock);

        move_list_safe(&physicalResource->iulist, &physical_free_list, &physicalUsedListLock, &physicalFreeListLock);
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
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    if (event->sleep == BUSY) {
        struct optEludeList * physicalResource = get_physical_resource(&physical_free_list, event->physical_id);
        bool move = true;
        if (!physicalResource) {
            move = false;
            physicalResource = get_physical_resource(&physical_used_list, event->physical_id);
            if (!physicalResource) {
                trace("TRACE: exiting rr_policy::on_sleep_state_change -- error\n");
                return 1;
            }
        }

        if (physicalResource->resource->virtualResource) {
            event->virtual_id = physicalResource->resource->virtualResource->resource->virtualId;
            rr_policy_save_context(event);
        }

        if (move)
            move_list_safe(&physicalResource->iulist, &physical_used_list, &physicalFreeListLock, &physicalUsedListLock);

        struct optEludeList* phys;
        printf("The free list is composed of : ");
        list_for_each_entry(phys, &physical_free_list, iulist) {
            printf("%lu ", phys->resource->physicalId);
        }
        printf("\n");
    } else if (event->sleep == AVAILABLE) {
        struct optEludeList * physicalResource = get_physical_resource(&physical_used_list, event->physical_id);
        if (!physicalResource) {
            trace("TRACE: exiting rr_policy::on_sleep_state_change -- error\n");
            return 1;
        }

        move_list_safe(&physicalResource->iulist, &physical_free_list, &physicalUsedListLock, &physicalFreeListLock);
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

    nb_resources = numberOfResource;
    pthread_mutex_init(&resourceListLock, NULL);
    pthread_mutex_init(&virtualValidQueueLock, NULL);
    pthread_mutex_init(&virtualInvalidQueueLock, NULL);
    pthread_mutex_init(&physicalFreeListLock, NULL);
    pthread_mutex_init(&physicalUsedListLock, NULL);

    for (unsigned long physical_id = 0; physical_id < nb_resources; physical_id++) {
        struct optEludeList * resource = (struct optEludeList*)malloc(sizeof(struct optEludeList));

        INIT_LIST_HEAD(&(resource->iulist));

        resource->resource = &(resourceList[physical_id]);

        list_add_tail(&(resource->iulist), &physical_free_list);
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

    list_for_each_safe(listIter, listIter_s, &virtual_on_ressource_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &virtual_valid_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &virtual_invalid_queue) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &physical_free_list) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &physical_used_list) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node);
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(NULL, EXIT, start, end);

    trace("TRACE: exiting rr_policy::exit\n");
}