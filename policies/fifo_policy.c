//
// Created by nathan on 19/06/24.
//

#include "fifo_policy.h"


pthread_mutex_t _fifo_resourceListLock;
int _fifo_nb_resources;




static inline int fifo_policy_select_phys_to_virtual(struct sOSEvent *event);
static inline int fifo_policy_select_virtual_to_evict(struct sOSEvent *event); // TODO:
static inline int fifo_policy_select_virtual_to_load(struct sOSEvent *event);
static inline int fifo_policy_save_context(struct sOSEvent *event); // TODO:
static inline int fifo_policy_restore_context(struct sOSEvent *event); // TODO:
static inline int fifo_policy_on_yield(struct sOSEvent *event);
static inline int fifo_policy_on_ready(struct sOSEvent *event);
static inline int fifo_policy_on_invalid(struct sOSEvent *event);
static inline int fifo_policy_on_hints(struct sOSEvent *event, struct HintsPayload* payload); // TODO:
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


static inline int fifo_policy_select_phys_to_virtual(struct sOSEvent *event) {
    trace("TRACE: entering fifo_policy::select_phys_to_virt\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optEludeList * phys = get_first_physical_available();

    if (!phys) {
        trace("TRACE: exiting fifo_policy::select_phys_to_virt -- error no free physical\n");
        return 1;
    }

    if (put_virtual_on_physical(event->virtual_id, phys->resource->physicalId)) {
        trace("TRACE: exiting fifo_policy::select_phys_to_virt -- error virtual not found\n");
        return 1;
    }

    event->physical_id = phys->resource->physicalId;
    struct optVirtualResourceList* virtualResource = phys->resource->virtualResource;
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
    return 1;
}

static inline int fifo_policy_select_virtual_to_load(struct sOSEvent* event) {
    // trace("TRACE: entering fifo_policy::select_virtual_to_load\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optEludeList * physicalResource = get_physical_resource( event->physical_id, &physical_available_list);

    if (!physicalResource) {
        printf("Error: physical %lu not found\n", event->physical_id);
        trace("TRACE: exiting fifo_policy::select_virtual_to_load -- error\n");
        return 1;
    }

    if (physicalResource->resource->virtualResource) {
//        trace("TRACE: exiting fifo_policy::select_virtual_to_load -- already in use\n");
        return 1;
    }

    struct optVirtualResourceList* virtualResource = get_first_virtual_valid();

    if (!virtualResource) {
        // trace("TRACE: exiting fifo_policy::select_virtual_to_load -- empty\n");
        return 1;
    }

    put_virtual_on_physical(virtualResource->resource->virtualId, physicalResource->resource->physicalId);

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

    if (get_virtual_off_physical(event->virtual_id, event->physical_id, true)) {
        printf("Error: virtual %lu not found or physical %lu not found.\n", event->virtual_id, event->physical_id);
        trace("TRACE: exiting fifo_policy::save_context -- error\n");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SAVE_CONTEXT, start, end);

    trace("TRACE: exiting fifo_policy::save_context\n");
    return 0;
}

static inline int fifo_policy_restore_context(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::restore_context\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    if (put_virtual_on_physical(event->virtual_id, event->physical_id)) {
        printf("Error: virtual %lu not found or physical %lu not found.\n", event->virtual_id, event->physical_id);
        trace("TRACE: exiting fifo_policy::restore_context -- error\n");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, RESTORE_CONTEXT, start, end);

    trace("TRACE: exiting fifo_policy::restore_context\n");
    return 0;
}

static inline int fifo_policy_on_yield(struct sOSEvent *event) {
    trace("TRACE: entering fifo_policy::on_yield\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    struct optVirtualResourceList* virtualResource;
    if (get_virtual_off_physical(event->virtual_id, event->physical_id, true)) {

    }
    virtualResource = get_virtual_resource(event->virtual_id, &virtual_valid_queue);


    if (!virtualResource) {
        trace("TRACE: exiting fifo_policy::on_yield -- error virtual not found\n");
        return 1;
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

    struct optVirtualResourceList* virtualResource = get_virtual_resource( event->virtual_id, &virtual_invalid_queue);
    if (!virtualResource) {
        trace("TRACE: exiting fifo_policy::on_ready -- error\n");
        return 1;
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->physical_resource = NULL;

    virtual_move_to(virtualResource, &virtual_invalid_queue, &virtual_valid_queue);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_READY, start, end);

    trace("TRACE: exiting fifo_policy::on_ready\n");
    return 0;
}

static inline int fifo_policy_on_invalid(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_invalid\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    if (get_virtual_off_physical(event->virtual_id, event->physical_id, false)) {

    }

    struct optVirtualResourceList* virtualResource = get_virtual_resource(event->virtual_id, &virtual_valid_queue);
    if (!virtualResource) {
        trace("TRACE: exiting fifo_policy::on_ready -- error\n");
        return 1;
    }

    virtualResource->resource->last_event_id = event->event_id;

    virtual_move_to(virtualResource, &virtual_valid_queue, &virtual_invalid_queue);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_INVALID, start, end);
    trace("TRACE: exiting fifo_policy::on_invalid\n");
    return 0;
}

static inline int fifo_policy_on_hints(struct sOSEvent* event, struct HintsPayload *payload) {
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

    struct optVirtualResourceList* virtualResource = add_virtual_resource(event->virtual_id, event->attached_process);

    virtualResource->resource->last_event_id = event->event_id;

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_CREATE_THREAD, start, end);
    trace("TRACE: exiting fifo_policy::on_create_thread\n");
    return 0;
}

static inline int fifo_policy_on_dead_thread(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_dead_thread\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    remove_virtual_resource(event->virtual_id);
    remove_virtual_resources_of_proc(event->attached_process);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_DEAD_THREAD, start, end);
    trace("TRACE: exiting fifo_policy::on_dead_thread\n");
    return 0;
}

static inline int fifo_policy_on_sleep_state_change(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_sleep_state_change\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    if (event->sleep == AVAILABLE) {
        struct optEludeList* physical = get_physical_resource(event->physical_id, &physical_used_list);

        if (!physical) {
            printf("Error: physical %lu not found\n", event->physical_id);
            trace("TRACE: exiting fifo_policy::on_sleep_state_change -- error\n");
            return 1;
        }

        physical_move_to(physical, &physical_used_list, &physical_available_list);
    } else if (event->sleep == BUSY) {
        struct optEludeList* physical = get_physical_resource(event->physical_id, &physical_available_list);

        if (!physical) {
            physical = get_physical_resource(event->physical_id, &physical_used_list);

            if (!physical) {
                printf("Error: physical %lu not found\n", event->physical_id);
                trace("TRACE: exiting fifo_policy::on_sleep_state_change -- error\n");
                return 1;
            }

            if (physical->resource->virtualResource) {
                event->virtual_id = physical->resource->virtualResource->resource->virtualId;
                fifo_policy_save_context(event);
            } else
                return 0;
        }

        physical_move_to(physical, &physical_available_list, &physical_used_list);
    }

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
    for (unsigned long physical_id = 0; physical_id < _fifo_nb_resources; physical_id++) {
        add_physical_resource(&resourceList[physical_id]);
    }

    virtual_valid_queue.sorted = true;
    virtual_valid_queue.asc = true;
    virtual_valid_queue.sortedBy = ID;

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

    list_for_each_safe(listIter, listIter_s, &virtual_on_resource_queue.lst) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &virtual_valid_queue.lst) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &virtual_invalid_queue.lst) {
        struct optVirtualResourceList* node = list_entry(listIter, struct optVirtualResourceList, iulist);

        list_del(listIter);

        free(node->resource);
        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &physical_available_list.lst) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node);
    }

    list_for_each_safe(listIter, listIter_s, &physical_used_list.lst) {
        struct optEludeList* node = list_entry(listIter, struct optEludeList, iulist);

        list_del(listIter);

        free(node);
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(NULL, EXIT, start, end);

    trace("TRACE: exiting fifo_policy::exit\n");
}