//
// Created by nathan on 19/06/24.
//

#include "fifo_policy.h"

LIST_HEAD(run_queue);
LIST_HEAD(running_queue);
LIST_HEAD(blocked_queue);

pthread_mutex_t resourceListLock;
int nb_resources;


struct optTaskList {
    struct task* task;
    struct list_head iulist;
    struct list_head proclist;
};

enum TaskState {
    Runnable,
    Running,
    Blocked
};

struct task {
    time_t last_commit;
    time_t runtime;
    unsigned long id;
    unsigned long seqnum;
    enum TaskState state;
};



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


static inline int fifo_policy_select_phys_to_virtual(struct sOSEvent *event) {
    trace("TRACE: entering fifo_policy::select_phys_to_virt\n");

    pthread_mutex_lock(&resourceListLock);
    for (int i = 0; i < nb_resources; i++) {
        if (resourceList[i].virtualId == 0) {
            resourceList[i].virtualId = event->virtual_id;
            pthread_mutex_unlock(&resourceListLock);
            event->physical_id = resourceList[i].physicalId;
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

    if (list_empty(&run_queue)) {
        // trace("TRACE: exiting fifo_policy::select_virtual_to_load -- empty\n");
        return 1;
    }

    struct optTaskList* task = list_first_entry(&run_queue, struct optTaskList, iulist);

    list_move_tail(&task->iulist, &running_queue);

    pthread_mutex_lock(&resourceListLock);

    resourceList[event->physical_id].virtualId = task->task->id;

    pthread_mutex_unlock(&resourceListLock);

    event->virtual_id = task->task->id;
    event->event_id = task->task->seqnum;

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

    struct optTaskList* taskList;
    list_for_each_entry(taskList, &running_queue, iulist) {
        if (taskList->task->id == event->virtual_id) {
            break;
        }
    }
    taskList->task->state = Runnable;
    taskList->task->seqnum = event->event_id;
    taskList->task->runtime += time(NULL) - taskList->task->last_commit;

    list_move_tail(&(taskList->iulist), &run_queue);

    pthread_mutex_lock(&resourceListLock);

    resourceList[event->physical_id].virtualId = 0;

    pthread_mutex_unlock(&resourceListLock);

    trace("TRACE: exiting fifo_policy::on_yield\n");
    return 0;
}

static inline int fifo_policy_on_ready(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_ready\n");

    struct optTaskList* taskList;
    list_for_each_entry(taskList, &blocked_queue, iulist) {
        printf("Looking at task %lu\n", taskList->task->id);
        if (taskList->task->id == event->virtual_id) {
            break;
        }
    }

    if (&(taskList->iulist) == &blocked_queue)
        return 1;

    taskList->task->state = Runnable;
    taskList->task->seqnum = event->event_id;

    list_move_tail(&(taskList->iulist), &run_queue);

    trace("TRACE: exiting fifo_policy::on_ready\n");
    return 0;
}

static inline int fifo_policy_on_invalid(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_invalid\n");

    struct optTaskList* taskList;
    list_for_each_entry(taskList, &running_queue, iulist) {
        if (taskList->task->id == event->virtual_id) {
            break;
        }
    }
    if (&(taskList->iulist) == &running_queue) {
        list_for_each_entry(taskList, &run_queue, iulist) {
            if (taskList->task->id == event->virtual_id) {
                break;
            }
        }
    }

    if (&(taskList->iulist) == &run_queue)
        return 1;

    taskList->task->state = Blocked;
    taskList->task->seqnum = event->event_id;
    taskList->task->runtime += time(NULL) - taskList->task->last_commit;

    list_move_tail(&(taskList->iulist), &blocked_queue);

    pthread_mutex_lock(&resourceListLock);

    resourceList[event->physical_id].virtualId = 0;

    pthread_mutex_unlock(&resourceListLock);

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

    struct optTaskList* taskList = (struct optTaskList*) malloc(sizeof (struct optTaskList));

    INIT_LIST_HEAD(&(taskList->iulist));

    taskList->task = (struct task*) malloc(sizeof (struct task));

    taskList->task->runtime = 0;
    taskList->task->last_commit = time(NULL);
    taskList->task->id = event->virtual_id;
    taskList->task->state = Blocked;

    list_add_tail(&(taskList->iulist), &blocked_queue);

    trace("TRACE: exiting fifo_policy::on_create_thread\n");
    return 0;
}

static inline int fifo_policy_on_dead_thread(struct sOSEvent* event) {
    trace("TRACE: entering fifo_policy::on_dead_thread\n");

    struct optTaskList* taskList;
    list_for_each_entry(taskList, &running_queue, iulist) {
        if (taskList->task->id == event->virtual_id) {
            break;
        }
    }

    if (&(taskList->iulist) == &running_queue) {
        list_for_each_entry(taskList, &run_queue, iulist) {
            if (taskList->task->id == event->virtual_id) {
                break;
            }
        }
    }
    if (&(taskList->iulist) == &run_queue) {
        list_for_each_entry(taskList, &blocked_queue, iulist) {
            if (taskList->task->id == event->virtual_id) {
                break;
            }
        }
    }
    if (&(taskList->iulist) == &blocked_queue) {
        return 1;
    }

    list_del(&(taskList->iulist));

    free(taskList->task);
    free(taskList);

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

    trace("TRACE: exiting fifo_policy::init\n");
    return 0;
}


void fifo_policy_exit() {
    trace("TRACE: entering fifo_policy::exit\n");

    struct list_head *listIter, *listIter_s;

    list_for_each_safe(listIter, listIter_s, &run_queue) {
        struct optTaskList* node = list_entry(listIter, struct optTaskList, iulist);

        list_del(listIter);

        free(node->task);
        free(node);
    }

    trace("TRACE: exiting fifo_policy::exit\n");
}