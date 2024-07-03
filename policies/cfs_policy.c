//
// Created by nathan on 01/07/24.
//

#include "cfs_policy.h"

pthread_mutex_t _cfs_resourceListLock;

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

static unsigned long min_granularity = 1000; // ns
static unsigned long latency = 10000; // ns

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

static inline int cfs_policy_select_phys_to_virtual(struct sOSEvent *event) {
    trace("TRACE: entering cfs_policy::select_phys_to_virt\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optEludeList * phys = get_first_physical_available();

    if (!phys) {
        trace("TRACE: exiting cfs_policy::select_phys_to_virt -- error no free physical\n");
        return 1;
    }

    if (put_virtual_on_physical(event->virtual_id, phys->resource->physicalId)) {
        trace("TRACE: exiting cfs_policy::select_phys_to_virt -- error virtual not found\n");
        return 1;
    }

    event->physical_id = phys->resource->physicalId;
    struct optVirtualResourceList* virtualResource = phys->resource->virtualResource;
    unsigned long tmp = virtualResource->resource->last_event_id;
    virtualResource->resource->last_event_id = event->event_id;

    event->event_id = tmp;

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SELECT_PHYS_TO_VIRTUAL, start, end);

    trace("TRACE: exiting cfs_policy::select_phys_to_virt\n");
    return 0;
}

static inline int preemption_time() {
    unsigned long nb_virtual = nb_virtual_resources();
    return ((nb_virtual + 1) * min_granularity > latency) ? min_granularity : (latency + nb_virtual) / (nb_virtual + 1);
}


static bool is_evict_able(struct optVirtualResourceList* virtual, struct timespec time) {
    return (time.tv_sec - virtual->resource->last_start.tv_sec) * 1000000
           + time.tv_nsec - virtual->resource->last_start.tv_nsec > preemption_time();
}


static inline int cfs_policy_select_virtual_to_evict(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::select_virtual_to_evict\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    pthread_mutex_lock(&physical_used_list.lock);
    struct optEludeList* physicalResource;
    list_for_each_entry(physicalResource, &physical_used_list.lst, iulist) {
        if (physicalResource->resource->virtualResource &&  is_evict_able(physicalResource->resource->virtualResource, start)) {
            event->physical_id = physicalResource->resource->physicalId;
            event->virtual_id = physicalResource->resource->virtualResource->resource->virtualId;
            pthread_mutex_lock(&physical_used_list.lock);
            return 0;
        }
    }
    pthread_mutex_lock(&physical_used_list.lock);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SELECT_VIRTUAL_TO_EVICT, start, end);

    trace("TRACE: exiting cfs_policy::select_virtual_to_evict\n");
    return 1;
}

static inline int cfs_policy_select_virtual_to_load(struct sOSEvent* event) {
    // trace("TRACE: entering cfs_policy::select_virtual_to_load\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    struct optEludeList * physicalResource = get_physical_resource( event->physical_id, &physical_available_list);

    if (!physicalResource) {
        physicalResource = get_physical_resource(event->physical_id, &physical_used_list);
        if (!physicalResource) {
            printf("Error: physical %lu not found", event->physical_id);
            trace("TRACE: exiting cfs_policy::select_virtual_to_load -- error\n");
            return 1;
        }
    }

    if (physicalResource->resource->virtualResource && !is_evict_able(physicalResource->resource->virtualResource, start)) {
//        trace("TRACE: exiting fifo_policy::select_virtual_to_load -- already in use\n");
        return 1;
    } else if (physicalResource->resource->virtualResource) {
        get_virtual_off_physical(physicalResource->resource->virtualResource->resource->virtualId, physicalResource->resource->physicalId, true);
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
    trace("TRACE: exiting cfs_policy::select_virtual_to_load\n");
    return 0;
}

static inline int cfs_policy_save_context(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::save_context\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    if (get_virtual_off_physical(event->virtual_id, event->physical_id, true)) {
        printf("Error: virtual %lu not found or physical %lu not found.\n", event->virtual_id, event->physical_id);
        trace("TRACE: exiting cfs_policy::save_context -- error\n");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, SAVE_CONTEXT, start, end);

    trace("TRACE: exiting cfs_policy::save_context\n");
    return 0;
}

static inline int cfs_policy_restore_context(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::restore_context\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    if (put_virtual_on_physical(event->virtual_id, event->physical_id)) {
        printf("Error: virtual %lu not found or physical %lu not found.\n", event->virtual_id, event->physical_id);
        trace("TRACE: exiting cfs_policy::restore_context -- error\n");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, RESTORE_CONTEXT, start, end);

    trace("TRACE: exiting cfs_policy::restore_context\n");
    return 0;
}

static inline int cfs_policy_on_yield(struct sOSEvent *event) {
    trace("TRACE: entering cfs_policy::on_yield\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);


    if (get_virtual_off_physical(event->virtual_id, event->physical_id, true)) {
        printf("Error: virtual %lu not found or physical %lu not found.\n", event->virtual_id, event->physical_id);
        trace("TRACE: exiting cfs_policy::on_yield -- error\n");
        return 1;
    }

    struct optVirtualResourceList* virtualResource = get_virtual_resource(event->virtual_id, &virtual_valid_queue);
    if (!virtualResource) {
        trace("TRACE: exiting cfs_policy::on_yield -- error virtual not found\n");
        return 1;
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

    struct optVirtualResourceList* virtualResource = get_virtual_resource( event->virtual_id, &virtual_invalid_queue);
    if (!virtualResource) {
        trace("TRACE: exiting cfs_policy::on_ready -- error\n");
        return 1;
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->physical_resource = NULL;

    virtual_move_to(virtualResource, &virtual_invalid_queue, &virtual_valid_queue);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_READY, start, end);

    trace("TRACE: exiting cfs_policy::on_ready\n");
    return 0;
}

static inline int cfs_policy_on_invalid(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::on_invalid\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    if (get_virtual_off_physical(event->virtual_id, event->physical_id, false)) {

    }

    struct optVirtualResourceList* virtualResource = get_virtual_resource(event->virtual_id, &virtual_valid_queue);
    if (!virtualResource) {
        trace("TRACE: exiting cfs_policy::on_ready -- error\n");
        return 1;
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->utilisation += (event->utilization * inverted_weights[virtualResource->resource->priority]) >> 22;

    virtual_move_to(virtualResource, &virtual_valid_queue, &virtual_invalid_queue);

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

    struct optVirtualResourceList* virtualResource = add_virtual_resource(event->virtual_id, event->attached_process);

    virtualResource->resource->last_event_id = event->event_id;

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_CREATE_THREAD, start, end);
    trace("TRACE: exiting cfs_policy::on_create_thread\n");
    return 0;
}

static inline int cfs_policy_on_dead_thread(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::on_dead_thread\n");


    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    remove_virtual_resource(event->virtual_id);
    remove_virtual_resources_of_proc(event->attached_process);

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    add_event(event, ON_DEAD_THREAD, start, end);
    trace("TRACE: exiting cfs_policy::on_dead_thread\n");
    return 0;
}

static inline int cfs_policy_on_sleep_state_change(struct sOSEvent* event) {
    trace("TRACE: entering cfs_policy::on_sleep_state_change\n");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    if (event->sleep == AVAILABLE) {
        struct optEludeList* physical = get_physical_resource(event->physical_id, &physical_used_list);

        if (!physical) {
            printf("Error: physical %lu not found\n", event->physical_id);
            trace("TRACE: exiting cfs_policy::on_sleep_state_change -- error\n");
            return 1;
        }

        physical_move_to(physical, &physical_used_list, &physical_available_list);
    } else if (event->sleep == BUSY) {
        struct optEludeList* physical = get_physical_resource(event->physical_id, &physical_available_list);

        if (!physical) {
            physical = get_physical_resource(event->physical_id, &physical_used_list);

            if (!physical) {
                printf("Error: physical %lu not found\n", event->physical_id);
                trace("TRACE: exiting cfs_policy::on_sleep_state_change -- error\n");
                return 1;
            }

            if (physical->resource->virtualResource) {
                event->virtual_id = physical->resource->virtualResource->resource->virtualId;
                cfs_policy_save_context(event);
            } else
                return 0;
        }

        physical_move_to(physical, &physical_available_list, &physical_used_list);
    }

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

    pthread_mutex_init(&_cfs_resourceListLock, NULL);

    for (unsigned long physical_id = 0; physical_id < numberOfResource; physical_id++) {
        add_physical_resource(&resourceList[physical_id]);
    }

    virtual_valid_queue.sorted = true;
    virtual_valid_queue.asc = true;
    virtual_valid_queue.sortedBy = UTILIZATION;


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

    trace("TRACE: exiting cfs_policy::exit\n");
}