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
    unsigned long nb;
};


extern struct Queue virtual_valid_queue;
extern struct Queue virtual_invalid_queue;
extern struct Queue virtual_on_resource_queue;

extern struct Queue physical_available_list;
extern struct Queue physical_used_list;

static unsigned long nb_virtual_resources() {
    return virtual_valid_queue.nb + virtual_on_resource_queue.nb + virtual_invalid_queue.nb;
}
static unsigned long nb_physical_resources() {
    return physical_used_list.nb + physical_available_list.nb;
}

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

void display_physical_list(struct Queue* queue);
void display_virtual_list(struct Queue* queue);


static void init_queues() {
    trace("TRACE: entering init_queues\n");
    INIT_LIST_HEAD(&(virtual_invalid_queue.lst));
    INIT_LIST_HEAD(&(virtual_valid_queue.lst));
    INIT_LIST_HEAD(&(virtual_on_resource_queue.lst));

    pthread_mutex_init(&(virtual_invalid_queue.lock), NULL);
    pthread_mutex_init(&(virtual_valid_queue.lock), NULL);
    pthread_mutex_init(&(virtual_on_resource_queue.lock), NULL);

    virtual_valid_queue.sorted = false;
    virtual_valid_queue.nb = 0;
    virtual_invalid_queue.sorted = false;
    virtual_invalid_queue.nb = 0;
    virtual_on_resource_queue.sorted = false;
    virtual_on_resource_queue.nb = 0;

    INIT_LIST_HEAD(&(physical_available_list.lst));
    INIT_LIST_HEAD(&(physical_used_list.lst));

    pthread_mutex_init(&(physical_available_list.lock), NULL);
    pthread_mutex_init(&(physical_used_list.lock), NULL);

    physical_available_list.sorted = false;
    physical_available_list.nb = 0;
    physical_used_list.sorted = false;
    physical_used_list.nb = 0;

    trace("TRACE: exiting init_queues\n");
}


#endif //SOS_QUEUES_H
