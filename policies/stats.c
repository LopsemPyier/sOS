//
// Created by nathan on 05/07/24.
//
#include "stats.h"


pthread_mutex_t stat_lock;

void init_stats_queues() {
    pthread_mutex_init(&stat_lock, NULL);
    FILE *fptr;
    fptr = fopen("stats.log", "w");
    fclose(fptr);
}

void store_queues() {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &time);

    FILE *fptr;
    fptr = fopen("stats.log", "a");
    fprintf(fptr, "TIME:%lu\n", time.tv_nsec+time.tv_sec*1000000);

    struct optEludeList *node_p, *tmp_p;
    fprintf(fptr, "USED");
    list_for_each_entry_safe(node_p, tmp_p, &physical_used_list.lst, iulist) {
        fprintf(fptr, ":%lu,%lu", node_p->resource->physicalId, (node_p->resource->virtualResource) ? node_p->resource->virtualResource->resource->virtualId : 0);
    }
    fprintf(fptr, "\n");
    fprintf(fptr, "AVAILABLE");
    list_for_each_entry_safe(node_p, tmp_p, &physical_available_list.lst, iulist) {
        fprintf(fptr, ":%lu", node_p->resource->physicalId);
    }

    fprintf(fptr, "\n");
    struct optVirtualResourceList *node_v, *tmp_v;
    fprintf(fptr, "ON_RESOURCE");
    list_for_each_entry_safe(node_v, tmp_v, &virtual_on_resource_queue.lst, iulist) {
        fprintf(fptr, ":%lu,%lu", node_v->resource->virtualId, (node_v->resource->physical_resource) ? node_v->resource->physical_resource->resource->physicalId : 0);
    }
    fprintf(fptr, "\n");
    fprintf(fptr, "VALID");
    list_for_each_entry_safe(node_v, tmp_v, &virtual_valid_queue.lst, iulist) {
        fprintf(fptr, ":%lu,%lu", node_v->resource->virtualId, node_v->resource->utilisation);
    }
    fprintf(fptr, "\n");
    fprintf(fptr, "INVALID");
    list_for_each_entry_safe(node_v, tmp_v, &virtual_invalid_queue.lst, iulist) {
        fprintf(fptr, ":%lu", node_v->resource->virtualId);
    }
    fprintf(fptr, "\n");

    fclose(fptr);
}