//
// Created by nathan on 18/06/24.
//

#ifndef SOS_API_H
#define SOS_API_H

#include "list.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <limits.h>
#include <sys/time.h>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define trace(msg) \
if (enable_api_trace)      \
printf(msg)


#ifdef TRACE
static const bool enable_api_trace = true;
#else
static const bool enable_api_trace = false;
#endif

extern int VIRTUAL_RESOURCE_SIZE;
extern int nb_resources;

enum eventType {
    INIT,
    EXIT,

    SELECT_PHYS_TO_VIRTUAL,
    SELECT_VIRTUAL_TO_EVICT,
    SELECT_VIRTUAL_TO_LOAD,
    SAVE_CONTEXT,
    RESTORE_CONTEXT,

    ON_YIELD,
    ON_READY,
    ON_INVALID,
    ON_HINTS,
    ON_PROTECTION_VIOLATION,
    ON_CREATE_THREAD,
    ON_DEAD_THREAD,
    ON_SLEEP_STATE_CHANGE,
    ON_SIGNAL
};

enum sleepType {
    BUSY,
    AVAILABLE
};

struct sOSEvent {
    unsigned long virtual_id;
    unsigned long physical_id;
    unsigned long virtual_nb;
    unsigned int attached_process;
    unsigned long event_id;
    struct list_head* eventPosition;
    enum sleepType sleep;
};

struct policy_function {
    int (*init)(unsigned long numberOfResources);
    int (*select_phys_to_virtual)(struct sOSEvent *event);
    int (*select_virtual_to_evict)(struct sOSEvent *event);
    int (*select_virtual_to_load)(struct sOSEvent *event);
    int (*save_context)(struct sOSEvent *event);
    int (*restore_context)(struct sOSEvent *event);

    int (*on_yield)(struct sOSEvent *event);
    int (*on_ready)(struct sOSEvent *event);
    int (*on_invalid)(struct sOSEvent *event);
    int (*on_hints)(struct sOSEvent *event);
    int (*on_protection_violation)(struct sOSEvent *event);
    int (*on_create_thread)(struct sOSEvent *event);
    int (*on_dead_thread)(struct sOSEvent *event);
    int (*on_sleep_state_change)(struct sOSEvent *event);
    int (*on_signal)(struct sOSEvent *event);

    void (*exit)();
};

struct policy_detail {
    char* name;
    struct policy_function* functions;
    bool is_default;
};


struct optEludeList {
    struct resource * resource;
    struct list_head iulist;
    struct list_head proclist;
};


struct optVirtualResourceList {
    struct virtual_resource * resource;
    struct list_head iulist;
    struct list_head proclist;
};


extern struct resource {
    unsigned long physicalId;
    unsigned long virtualId;
    int process;
    void *usedListPositionPointer;
    struct list_head * processUsedListPointer;
    struct optVirtualResourceList * virtualResource;
    pthread_mutex_t lock;
} * resourceList;


struct virtual_resource {
    unsigned long virtualId;
    struct optEludeList * physical_resource;
    int process;
    unsigned long utilisation;
    struct timespec last_start;
    unsigned long last_event_id;
};


static inline void insertResource(struct resource **resourceList, unsigned long physicalId, int pos) {
    ((*resourceList)+pos)->physicalId = physicalId;
    ((*resourceList)+pos)->virtualResource = NULL;
    ((*resourceList)+pos)->process = 0;
}

struct p_args_p {
    int origin, ret/*, nb_unacct*/; /* needs to be taken before giving off com. zone... sadly.. */
    unsigned long addr;
    struct list_head *l_ps;
};


extern char const *filename;

static void init_statistics() {
    FILE *fptr;
    fptr = fopen(filename, "w");
    fclose(fptr);
}

static void add_event(struct sOSEvent* event, enum eventType eventType, struct timespec start, struct timespec end) {
    FILE *fptr;
    fptr = fopen(filename, "a");

    fprintf(fptr, "%ld;", start.tv_sec * 1000000000 + start.tv_nsec);
    fprintf(fptr, "%ld;", (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec));
    switch (eventType) {
        case INIT:
            fprintf(fptr, "%s;", "INIT");
            break;
        case EXIT:
            fprintf(fptr, "%s;", "EXIT");
            break;

        case SELECT_PHYS_TO_VIRTUAL:
            fprintf(fptr, "%s;", "SELECT_PHYS_TO_VIRTUAL");
            break;
        case SELECT_VIRTUAL_TO_EVICT:
            fprintf(fptr, "%s;", "SELECT_VIRTUAL_TO_EVICT");
            break;
        case SELECT_VIRTUAL_TO_LOAD:
            fprintf(fptr, "%s;", "SELECT_VIRTUAL_TO_LOAD");
            break;
        case SAVE_CONTEXT:
            fprintf(fptr, "%s;", "SAVE_CONTEXT");
            break;
        case RESTORE_CONTEXT:
            fprintf(fptr, "%s;", "RESTORE_CONTEXT");
            break;

        case ON_YIELD:
            fprintf(fptr, "%s;", "ON_YIELD");
            break;
        case ON_READY:
            fprintf(fptr, "%s;", "ON_READY");
            break;
        case ON_INVALID:
            fprintf(fptr, "%s;", "ON_INVALID");
            break;
        case ON_HINTS:
            fprintf(fptr, "%s;", "ON_HINTS");
            break;
        case ON_PROTECTION_VIOLATION:
            fprintf(fptr, "%s;", "ON_PROTECTION_VIOLATION");
            break;
        case ON_CREATE_THREAD:
            fprintf(fptr, "%s;", "ON_CREATE_THREAD");
            break;
        case ON_DEAD_THREAD:
            fprintf(fptr, "%s;", "ON_DEAD_THREAD");
            break;
        case ON_SLEEP_STATE_CHANGE:
            fprintf(fptr, "%s;", "ON_SLEEP_STATE_CHANGE");
            break;
        case ON_SIGNAL:
            fprintf(fptr, "%s;", "ON_SIGNAL");
            break;
    }
    if (event) {
        fprintf(fptr, "%lu;%lu;%du;%lu", event->virtual_id, event->physical_id, event->attached_process, event->event_id);
    } else {
        fprintf(fptr, ";;;");

    }
    fprintf(fptr, "\n");
    fclose(fptr);
}



extern int submitResourceList(struct sOSEvent* event, struct list_head* resource_to_submit);

#endif //SOS_API_H
