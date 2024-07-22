#include "queues.h"


struct Queue virtual_valid_queue;
struct Queue virtual_invalid_queue;
struct Queue virtual_on_resource_queue;

struct Queue physical_available_list;
struct Queue physical_used_list;


void display_physical_list(struct Queue* queue) {
    struct optEludeList* phys;
    int nb = 0;
    list_for_each_entry(phys, &queue->lst, iulist) {
        printf("%lu ", phys->resource->physicalId);
        nb++;
        if (nb >= 10)
            return;
    }
}

void display_virtual_queue(struct Queue* queue) {
    struct optVirtualResourceList* node;
    int nb = 0;
    list_for_each_entry(node, &queue->lst, iulist) {
        printf("%lu ", node->resource->virtualId);
        if (queue->sorted) {
            if (queue->sortedBy == UTILIZATION)
                printf("(%lu) ", node->resource->utilisation);
        }
        nb++;
        if (nb >= 10)
            return;
    }
}


struct optEludeList * get_physical_resource(unsigned long id, struct Queue * queue) {
    printf("Get physical resource\n");
    printf("Available list : ");
    display_physical_list(&physical_available_list);
    printf("\n");
    printf("Used list : ");
    display_physical_list(&physical_used_list);
    printf("\n");

    if (list_empty(&queue->lst)) {
        printf("Empty list anyways\n");
        return NULL;
    }
    struct optEludeList *res,*tmp;
    list_for_each_entry_safe(res, tmp, &queue->lst, iulist) {
        if (res->resource->physicalId == id) {
            break;
        }
    }
    printf("Exiting with value %p\n", res);
    if (&res->iulist == &queue->lst)
        return NULL;
    else
        return res;
}

struct optVirtualResourceList * get_virtual_resource(unsigned long id, struct Queue * queue) {
    printf("Get virtual resource\n");
    printf("On Resource queue : ");
    display_virtual_queue(&virtual_on_resource_queue);
    printf("\n");
    printf("Valid queue : ");
    display_virtual_queue(&virtual_valid_queue);
    printf("\n");
    printf("Invalid queue : ");
    display_virtual_queue(&virtual_invalid_queue);
    printf("\n");

    if (list_empty(&queue->lst)) {
        printf("Empty list anyways\n");
        return NULL;
    }
    struct optVirtualResourceList *res,*tmp;
    list_for_each_entry_safe(res, tmp, &queue->lst, iulist) {
        if (res->resource->virtualId == id)
            break;
    }
    printf("Exiting with value %p\n", res);
    if (&res->iulist == &queue->lst)
        return NULL;
    else
        return res;

}

int put_virtual_on_physical(unsigned long virtual_id, unsigned long physical_id) {
    printf("Put virtual on physical\n");
    struct optVirtualResourceList* virtual = get_virtual_resource(virtual_id, &virtual_valid_queue);

    if (!virtual) {
        printf("No virtual\n");
        return 1;
    }

    struct optEludeList* physical = get_physical_resource(physical_id, &physical_available_list);

    if (!physical) {
        printf("No physical\n");
        return 1;
    }

    if (physical->resource->virtualResource) {
        printf("Physical still has virtual\n");
        return 1;
    }

    printf("Moving physical\n");
    physical_move_to(physical, &physical_available_list, &physical_used_list);
    printf("Moving virtual\n");
    virtual_move_to(virtual, &virtual_valid_queue, &virtual_on_resource_queue);

    virtual->resource->physical_resource = physical;
    physical->resource->virtualResource = virtual;

    return 0;
}


int get_virtual_off_physical(unsigned long virtual_id, unsigned long physical_id, bool valid) {
    printf("Get virtual off physical\n");
    struct optVirtualResourceList* virtual = get_virtual_resource(virtual_id, &virtual_on_resource_queue);

    if (!virtual) {
        printf("No virtual\n");
        return 1;
    }

    struct optEludeList* physical = get_physical_resource(physical_id, &physical_used_list);

    if (!physical) {
        printf("No physical\n");
        return 1;
    }

    printf("Virtual %p is supposed to be the same as physical->virtual %p\n", virtual, physical->resource->virtualResource);

    /*if (!physical->resource->virtualResource || physical->resource->virtualResource != virtual) {
        printf("No virtual or wrong one\n");
        return 1;
    }*/


    physical_move_to(physical, &physical_used_list, &physical_available_list);
    if (valid)
        virtual_move_to(virtual, &virtual_on_resource_queue, &virtual_valid_queue);
    else
        virtual_move_to(virtual, &virtual_on_resource_queue, &virtual_invalid_queue);

    virtual->resource->physical_resource = NULL;
    physical->resource->virtualResource = NULL;

    return 0;
}


void insert_to_queue(struct optVirtualResourceList* item, struct Queue * queue) {
    if (queue->sorted) {
        insert_to_queue_sorted(item, queue, queue->asc, queue->sortedBy);
    } else {
        pthread_mutex_lock(&queue->lock);
        list_move_tail(&item->iulist, &queue->lst);
        queue->nb++;
        pthread_mutex_unlock(&queue->lock);
    }
}

void insert_to_queue_sorted(struct optVirtualResourceList* item, struct Queue * queue, bool asc, enum SortedBy sortedBy) {
    struct optVirtualResourceList* ptr;
    pthread_mutex_lock(&queue->lock);
    list_for_each_entry(ptr, &queue->lst, iulist) {
        if (sortedBy == UTILIZATION && asc && ptr->resource->utilisation > item->resource->utilisation) {
            list_move(&item->iulist, &ptr->iulist);
            queue->nb++;
            pthread_mutex_unlock(&queue->lock);
            return;
        }
        else if (sortedBy == UTILIZATION && !asc && ptr->resource->utilisation < item->resource->utilisation) {
            list_move(&item->iulist, &ptr->iulist);
            queue->nb++;
            pthread_mutex_unlock(&queue->lock);
            return;
        }
        else if (sortedBy == ID && asc && ptr->resource->virtualId < item->resource->virtualId) {
            list_move(&item->iulist, &ptr->iulist);
            queue->nb++;
            pthread_mutex_unlock(&queue->lock);
            return;
        }
        else if (sortedBy == ID && !asc && ptr->resource->virtualId > item->resource->virtualId) {
            list_move(&item->iulist, &ptr->iulist);
            queue->nb++;
            pthread_mutex_unlock(&queue->lock);
            return;
        }
    }
    list_move(&item->iulist, queue->lst.prev);
    queue->nb++;
    pthread_mutex_unlock(&queue->lock);
}


void physical_move_to(struct optEludeList* item, struct Queue* src, struct Queue* dst) {
    LIST_HEAD(tmp);

    pthread_mutex_lock(&src->lock);
    list_move_tail(&item->iulist, &tmp);
    src->nb--;
    pthread_mutex_unlock(&src->lock);

    pthread_mutex_lock(&dst->lock);
    list_move(&item->iulist, &dst->lst);
    dst->nb++;
    pthread_mutex_unlock(&dst->lock);
}

void virtual_move_to(struct optVirtualResourceList* item, struct Queue* src, struct Queue* dst) {
    LIST_HEAD(tmp);

    pthread_mutex_lock(&src->lock);
    list_move_tail(&item->iulist, &tmp);
    src->nb--;
    pthread_mutex_unlock(&src->lock);

    insert_to_queue(item, dst);
}

struct optVirtualResourceList * add_virtual_resource(unsigned long id, unsigned long proc) {
    struct optVirtualResourceList* virtualResource;
    virtualResource = get_virtual_resource(id, &virtual_on_resource_queue);
    if (!virtualResource) {
        virtualResource = get_virtual_resource(id, &virtual_valid_queue);
        if (!virtualResource) {
            virtualResource = get_virtual_resource(id, &virtual_invalid_queue);
            if (!virtualResource) {
                virtualResource = (struct optVirtualResourceList*) malloc(sizeof (struct optVirtualResourceList));

                INIT_LIST_HEAD(&(virtualResource->iulist));

                virtualResource->resource = (struct virtual_resource*) malloc(sizeof (struct virtual_resource));
                virtualResource->resource->physical_resource = NULL;
                INIT_LIST_HEAD(&virtualResource->resource->physical_affinity);

                pthread_mutex_lock(&virtual_invalid_queue.lock);
                list_add_tail(&(virtualResource->iulist), &virtual_invalid_queue.lst);
                virtual_invalid_queue.nb++;
                pthread_mutex_unlock(&virtual_invalid_queue.lock);
            }
        } else {
            virtual_move_to(virtualResource, &virtual_valid_queue, &virtual_invalid_queue);
        }
    } else {
        virtual_move_to(virtualResource, &virtual_on_resource_queue, &virtual_invalid_queue);
    }
    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        physical_move_to(physicalResource, &physical_used_list, &physical_available_list);
        physicalResource->resource->virtualResource = NULL;
    }
    virtualResource->resource->physical_resource = NULL;
    virtualResource->resource->virtualId = id;
    virtualResource->resource->last_event_id = 0;
    virtualResource->resource->process = proc;
    virtualResource->resource->utilisation = 0;
    virtualResource->resource->priority = 0;

    return virtualResource;
}

struct optEludeList * add_physical_resource(struct resource *physical) {
    struct optEludeList* physicalResource = (struct optEludeList*) malloc(sizeof (struct optEludeList));

    INIT_LIST_HEAD(&(physicalResource->iulist));

    physicalResource->resource = physical;

    pthread_mutex_lock(&physical_available_list.lock);
    list_add_tail(&(physicalResource->iulist), &physical_available_list.lst);
    physical_available_list.nb++;
    pthread_mutex_unlock(&physical_available_list.lock);

    return physicalResource;
}

void remove_virtual_resource(unsigned long id) {
    struct optVirtualResourceList* virtualResource = get_virtual_resource(id, &virtual_on_resource_queue);
    if (unlikely(!virtualResource)) {
        virtualResource = get_virtual_resource(id, &virtual_valid_queue);
        if (unlikely(!virtualResource)) {
            virtualResource = get_virtual_resource(id, &virtual_invalid_queue);
            if (unlikely(!virtualResource)) {
                return;
            } else {
                pthread_mutex_lock(&virtual_invalid_queue.lock);
                list_del(&(virtualResource->iulist));
                virtual_invalid_queue.nb--;
                pthread_mutex_unlock(&virtual_invalid_queue.lock);
            }
        } else {
            pthread_mutex_lock(&virtual_valid_queue.lock);
            list_del(&(virtualResource->iulist));
            virtual_valid_queue.nb--;
            pthread_mutex_unlock(&virtual_valid_queue.lock);
        }
    } else {
        pthread_mutex_lock(&virtual_on_resource_queue.lock);
        list_del(&(virtualResource->iulist));
        virtual_on_resource_queue.nb--;
        pthread_mutex_unlock(&virtual_on_resource_queue.lock);
    }

    if (virtualResource->resource->physical_resource) {
        struct optEludeList* physicalResource = virtualResource->resource->physical_resource;
        pthread_mutex_lock(&physicalResource->resource->lock);
        physicalResource->resource->virtualResource = NULL;
        pthread_mutex_unlock(&physicalResource->resource->lock);

        physical_move_to(physicalResource, &physical_used_list, &physical_available_list);
    }

    free(virtualResource->resource);
    free(virtualResource);
}

void remove_virtual_resources_of_proc(unsigned long proc) {
    struct optVirtualResourceList *item, *tmp;
    list_for_each_entry_safe(item, tmp, &virtual_on_resource_queue.lst, iulist) {
        if (item->resource->process == proc) {
            remove_virtual_resource(item->resource->virtualId);
        }
    }
    list_for_each_entry_safe(item, tmp, &virtual_valid_queue.lst, iulist) {
        if (item->resource->process == proc) {
            remove_virtual_resource(item->resource->virtualId);
        }
    }
    list_for_each_entry_safe(item, tmp, &virtual_invalid_queue.lst, iulist) {
        if (item->resource->process == proc) {
            remove_virtual_resource(item->resource->virtualId);
        }
    }
}

struct optEludeList* get_first_physical_available() {
    if (some_physical_available())
        return list_first_entry(&physical_available_list.lst, struct optEludeList, iulist);
    else
        return NULL;
}
bool some_physical_available() {
    return !list_empty(&physical_available_list.lst);
}
struct optVirtualResourceList* get_first_virtual_valid() {
    if (some_virtual_valid())
        return list_first_entry(&virtual_valid_queue.lst, struct optVirtualResourceList, iulist);
    else
        return NULL;
}
bool some_virtual_valid() {
    return !list_empty(&virtual_valid_queue.lst);
}
bool is_physical_available_by_id(unsigned long id) {
    struct optEludeList* physical = get_physical_resource(id, &physical_available_list);
    if (!physical) {
        return false;
    } else {
        return is_physical_available_by_resource(physical->resource);
    }
}
bool is_physical_available_by_resource(struct resource* physical) {
    if (physical->virtualResource)
        return false;
    else
        return true;
}
