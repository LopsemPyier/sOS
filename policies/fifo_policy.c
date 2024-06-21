//
// Created by nathan on 19/06/24.
//

#include "fifo_policy.h"

LIST_HEAD(virtual_on_ressource_queue);
LIST_HEAD(virtual_valid_queue);
LIST_HEAD(virtual_invalid_queue);

pthread_mutex_t resourceListLock;
pthread_mutex_t virtualOnResourceQueueLock;
pthread_mutex_t virtualValidQueueLock;
pthread_mutex_t virtualInvalidQueueLock;
int nb_resources;




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

void move_list_safe(struct list_head * item, struct list_head * dest, pthread_mutex_t* lock1, pthread_mutex_t* lock2) {
    LIST_HEAD(tmp);

    pthread_mutex_lock(lock1);
    list_move_tail(item, &tmp);
    pthread_mutex_unlock(lock1);

    pthread_mutex_lock(lock2);
    list_move_tail(item, dest);
    pthread_mutex_unlock(lock2);
}


static inline int fifo_policy_select_phys_to_virtual(struct sOSEvent *event) {
    trace("TRACE: entering fifo_policy::select_phys_to_virt\n");

    /*struct optVirtualResourceList* virtualResource;
    list_for_each_entry(virtualResource, &virtual_valid_queue, iulist) {
        if (virtualResource->resource->virtualId == event->virtual_id) {
            break;
        }
    }
    if (&(virtualResource->iulist) == &virtual_valid_queue) {
        trace("TRACE: exiting fifo_policy::select_phys_to_virt -- error\n");
        return 1;
    }*/

    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_valid_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting fifo_policy::on_ready -- error\n");
        return 1;
    }

    pthread_mutex_lock(&resourceListLock);
    for (int i = 0; i < nb_resources; i++) {
        if (resourceList[i].virtualId == 0) {
            resourceList[i].virtualId = event->virtual_id;
            pthread_mutex_unlock(&resourceListLock);
            event->physical_id = resourceList[i].physicalId;
            virtualResource->resource->physicalId = event->physical_id;
            virtualResource->resource->last_event_id = event->event_id;
            return 0;
        }
    }
    pthread_mutex_unlock(&resourceListLock);

    // fifo_policy_select_virtual_to_evict(event);

    // fifo_policy_save_context(event);

    // fifo_policy_restore_context(event);

    trace("TRACE: exiting fifo_policy::select_phys_to_virt\n");
    return 1;
}

static inline int fifo_policy_select_virtual_to_evict(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::select_virtual_to_evict\n");
    trace("TRACE: exiting fifo_policy::select_virtual_to_evict\n");
    return 0;
}

static inline int fifo_policy_select_virtual_to_load(struct sOSEvent* event) {
    // trace("TRACE: entering fifo_policy::select_virtual_to_load\n");

    if (list_empty(&virtual_valid_queue)) {
        // trace("TRACE: exiting fifo_policy::select_virtual_to_load -- empty\n");
        return 1;
    }

    struct optVirtualResourceList* virtualResource = list_first_entry(&virtual_valid_queue, struct optVirtualResourceList, iulist);

    pthread_mutex_lock(&resourceListLock);

    resourceList[event->physical_id].virtualId = virtualResource->resource->virtualId;
    resourceList[event->physical_id].virtualResource = &virtualResource->iulist;

    pthread_mutex_unlock(&resourceListLock);

    event->virtual_id = virtualResource->resource->virtualId;
    event->event_id = virtualResource->resource->last_event_id;

    move_list_safe(&(virtualResource->iulist), &virtual_on_ressource_queue, &virtualValidQueueLock, &virtualOnResourceQueueLock);

    printf("Selected task %lu to assigned to cpu %lu\n", event->virtual_id, event->physical_id);

    trace("TRACE: exiting fifo_policy::select_virtual_to_load\n");
    return 0;
}

static inline int fifo_policy_save_context(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::save_context\n");
    trace("TRACE: exiting fifo_policy::save_context\n");
    return 0;
}

static inline int fifo_policy_restore_context(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::restore_context\n");
    trace("TRACE: exiting fifo_policy::restore_context\n");
    return 0;
}

static inline int fifo_policy_on_yield(struct sOSEvent *event) {
    trace("TRACE: entering fifo_policy::on_yield\n");

    //struct optVirtualResourceList* virtualResource;
/*    if (get_virtual_resource(virtualResource, &virtual_valid_queue, &virtualValidQueueLock, event->virtual_id)) {
        return 1;
    }*/

    /*list_for_each_entry(virtualResource, &virtual_on_ressource_queue, iulist) {
        if (virtualResource->resource->virtualId == event->virtual_id) {
            break;
        }
    }
    if (&(virtualResource->iulist) == &virtual_on_ressource_queue) {
        trace("TRACE: exiting fifo_policy::on_yield -- error\n");
        return 1;
    }*/

    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting fifo_policy::on_ready -- error\n");
        return 1;
    }

    virtualResource->resource->last_event_id = event->event_id;

    pthread_mutex_lock(&resourceListLock);

    if (virtualResource->resource->physicalId != NO_RESOURCE)
        resourceList[event->physical_id].virtualId = 0;

    pthread_mutex_unlock(&resourceListLock);

    move_list_safe(&(virtualResource->iulist), &virtual_valid_queue, &virtualOnResourceQueueLock, &virtualValidQueueLock);


    virtualResource->resource->physicalId = NO_RESOURCE;

    trace("TRACE: exiting fifo_policy::on_yield\n");
    return 0;
}

static inline int fifo_policy_on_ready(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_ready\n");

    //struct optVirtualResourceList* virtualResource;
    /*if (get_virtual_resource(virtualResource, &virtual_invalid_queue, &virtualInvalidQueueLock, event->virtual_id)) {
        return 1;
    }*/

    /*list_for_each_entry(virtualResource, &virtual_invalid_queue, iulist) {
        if (virtualResource->resource->virtualId == event->virtual_id) {
            break;
        }
    }
    if (&(virtualResource->iulist) == &virtual_invalid_queue) {
        trace("TRACE: exiting fifo_policy::on_ready -- error\n");
        return 1;
    }*/
    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_invalid_queue, event->virtual_id);
    if (!virtualResource) {
        trace("TRACE: exiting fifo_policy::on_ready -- error\n");
        return 1;
    }

    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->physicalId = NO_RESOURCE;

    move_list_safe(&(virtualResource->iulist), &virtual_valid_queue, &virtualInvalidQueueLock, &virtualValidQueueLock);


    trace("TRACE: exiting fifo_policy::on_ready\n");
    return 0;
}

static inline int fifo_policy_on_invalid(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_invalid\n");

    //struct optVirtualResourceList* virtualResource;

    bool on_resource = true;
    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        on_resource = false;
        virtualResource = get_virtual_resource(&virtual_valid_queue, event->virtual_id);
        if(!virtualResource) {
            trace("TRACE: exiting fifo_policy::on_ready -- error\n");
            return 1;
        }
    }

/*    list_for_each_entry(virtualResource, &virtual_on_ressource_queue, iulist) {
        if (virtualResource->resource->virtualId == event->virtual_id) {
            break;
        }
    }
    if (&(virtualResource->iulist) == &virtual_on_ressource_queue) {
        on_resource = false;
        list_for_each_entry(virtualResource, &virtual_valid_queue, iulist) {
            if (virtualResource->resource->virtualId == event->virtual_id) {
                break;
            }
        }
        if (&(virtualResource->iulist) == &virtual_valid_queue) {
            trace("TRACE: exiting fifo_policy::on_invalid -- error\n");
            return 1;
        }
    }*/

    virtualResource->resource->last_event_id = event->event_id;

    pthread_mutex_lock(&resourceListLock);

    if (virtualResource->resource->physicalId != NO_RESOURCE)
        resourceList[event->physical_id].virtualId = 0;

    pthread_mutex_unlock(&resourceListLock);

    virtualResource->resource->physicalId = NO_RESOURCE;

    if (on_resource)
        move_list_safe(&(virtualResource->iulist), &virtual_invalid_queue, &virtualOnResourceQueueLock, &virtualInvalidQueueLock);
    else
        move_list_safe(&(virtualResource->iulist), &virtual_invalid_queue, &virtualValidQueueLock, &virtualInvalidQueueLock);

    trace("TRACE: exiting fifo_policy::on_invalid\n");
    return 0;
}

static inline int fifo_policy_on_hints(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_hints\n");
    trace("TRACE: exiting fifo_policy::on_hints\n");
    return 0;
}

static inline int fifo_policy_on_protection_violation(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_protection_violation\n");
    trace("TRACE: exiting fifo_policy::on_protection_violation\n");
    return 0;
}

static inline int fifo_policy_on_create_thread(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_create_thread\n");

    struct optVirtualResourceList* virtualResource = (struct optVirtualResourceList*) malloc(sizeof (struct optVirtualResourceList));

    INIT_LIST_HEAD(&(virtualResource->iulist));

    virtualResource->resource = (struct virtual_resource*) malloc(sizeof (struct virtual_resource));

    virtualResource->resource->physicalId = NO_RESOURCE;
    virtualResource->resource->virtualId = event->virtual_id;
    virtualResource->resource->last_event_id = event->event_id;
    virtualResource->resource->process = event->attached_process;
    virtualResource->resource->utilisation = 0;

    pthread_mutex_lock(&virtualInvalidQueueLock);
    list_add_tail(&(virtualResource->iulist), &virtual_invalid_queue);
    pthread_mutex_unlock(&virtualInvalidQueueLock);

    trace("TRACE: exiting fifo_policy::on_create_thread\n");
    return 0;
}

static inline int fifo_policy_on_dead_thread(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_dead_thread\n");

    /*struct optVirtualResourceList* virtualResource;


    list_for_each_entry(virtualResource, &virtual_on_ressource_queue, iulist) {
        if (virtualResource->resource->virtualId == event->virtual_id) {
            break;
        }
    }
    if (&(virtualResource->iulist) == &virtual_on_ressource_queue) {
        list_for_each_entry(virtualResource, &virtual_valid_queue, iulist) {
            if (virtualResource->resource->virtualId == event->virtual_id) {
                break;
            }
        }
        if (&(virtualResource->iulist) == &virtual_valid_queue) {
            list_for_each_entry(virtualResource, &virtual_invalid_queue, iulist) {
                if (virtualResource->resource->virtualId == event->virtual_id) {
                    break;
                }
            }
            if (&(virtualResource->iulist) == &virtual_invalid_queue) {
                trace("TRACE: exiting fifo_policy::on_dead_thread -- error\n");
                return 1;
            }
        }
    }*/
    struct optVirtualResourceList* virtualResource = get_virtual_resource(&virtual_on_ressource_queue, event->virtual_id);
    if (!virtualResource) {
        virtualResource = get_virtual_resource(&virtual_valid_queue, event->virtual_id);
        if (!virtualResource) {
            virtualResource = get_virtual_resource(&virtual_invalid_queue, event->virtual_id);
            if (!virtualResource) {
                trace("TRACE: exiting fifo_policy::on_dead_thread -- error\n");
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

    free(virtualResource->resource);
    free(virtualResource);

    trace("TRACE: exiting fifo_policy::on_dead_thread\n");
    return 0;
}

static inline int fifo_policy_on_sleep_state_change(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_sleep_state_change\n");
    trace("TRACE: exiting fifo_policy::on_sleep_state_change\n");
    return 0;
}

static inline int fifo_policy_on_signal(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_signal\n");
    trace("TRACE: exiting fifo_policy::on_signal\n");
    return 0;
}

static inline int fifo_policy_init(unsigned long numberOfResource) {
    trace("TRACE: entering fifo_policy::init\n");

    nb_resources = numberOfResource;
    pthread_mutex_init(&resourceListLock, NULL);
    pthread_mutex_init(&virtualValidQueueLock, NULL);
    pthread_mutex_init(&virtualInvalidQueueLock, NULL);

    trace("TRACE: exiting fifo_policy::init\n");
    return 0;
}


void fifo_policy_exit() {
    trace("TRACE: entering fifo_policy::exit\n");

    struct list_head *listIter, *listIter_s;

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

    trace("TRACE: exiting fifo_policy::exit\n");
}