//
// Created by nathan on 01/07/24.
//

#ifndef SOS_QUEUES_H
#define SOS_QUEUES_H

#include "api.h"

enum SortedBy {
    UTILIZATION,
    ID,
};

struct Queue {
    struct list_head lst;
    pthread_mutex_t lock;
    bool sorted;
    bool asc;
    enum SortedBy sortedBy;
};


extern struct Queue virtual_valid_queue;
extern struct Queue virtual_invalid_queue;
extern struct Queue virtual_on_resource_queue;

extern struct Queue physical_available_list;
extern struct Queue physical_used_list;

void init_queues();

struct optEludeList * get_physical_resource(unsigned long id, struct Queue * queue);
struct optVirtualResourceList * get_virtual_resource(unsigned long id, struct Queue * queue);

int put_virtual_on_physical(unsigned long virtual_id, unsigned long physical_id);
int get_virtual_off_physical(unsigned long virtual_id, unsigned long physical_id, bool valid);

void physical_move_to(struct optEludeList* item, struct Queue* src, struct Queue* dst);
void virtual_move_to(struct optVirtualResourceList* item, struct Queue* src, struct Queue* dst);

struct optVirtualResourceList * add_virtual_resource(unsigned long id, unsigned long proc);
struct optEludeList * add_physical_resource(struct resource *physical);

void remove_virtual_resource(unsigned long id);
void remove_virtual_resources_of_proc(unsigned long proc);

void insert_to_queue(struct optVirtualResourceList* item, struct Queue * queue);
void insert_to_queue_sorted(struct optVirtualResourceList* item, struct Queue * queue, bool asc, enum SortedBy sortedBy);

struct optEludeList* get_first_physical_available();
bool some_physical_available();
struct optVirtualResourceList* get_first_virtual_valid();
bool some_virtual_valid();
bool is_physical_available_by_id(unsigned long id);
bool is_physical_available_by_resource(struct resource* physical);


static void display_physical_list(struct Queue* queue) {
    struct optEludeList* phys;
    list_for_each_entry(phys, &queue->lst, iulist) {
        printf("%lu ", phys->resource->physicalId);
    }
}

static void display_virtual_queue(struct Queue* queue) {
    struct optVirtualResourceList* node;
    list_for_each_entry(node, &queue->lst, iulist) {
        printf("%lu ", node->resource->virtualId);
    }
}

#endif //SOS_QUEUES_H
