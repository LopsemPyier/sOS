#include "../incl/linkedApi.h"


#define swap_function_name(name) usm_swap_##name
#define usm_swap(name, policy) \
static inline int swap_function_name(name) (struct usm_event *usmEvent) { \
    return usm_swap_impl(usmEvent, policy); \
}
#define free_function_name(name) usm_free_##name
#define usm_free(name, policy) \
static inline int free_function_name(name) (struct usm_event *usmEvent) { \
    return usm_free_impl(usmEvent, policy); \
}
#define wrap_policy(name, policy) \
usm_swap(name, policy) \
usm_free(name, policy)

#define usm_ops(name) {.usm_swap=&swap_function_name(name),.usm_free=&free_function_name(name)}

#define usm_swap_policy_ops_name(name) usm_swap_##name##_ops

#define create_usm_bindings(name,policy) \
wrap_policy(name, policy.functions)     \
struct usm_swap_policy_ops usm_swap_policy_ops_name(name) = usm_ops(name);
#define register_policy(policy_name,policy) \
if (usm_register_swap_policy(&usm_swap_policy_ops_name(policy_name), policy.name, policy.is_default)) \
    return 1;


pthread_mutex_t policiesSet1Swaplock;
LIST_HEAD(swapList);

struct list_head * getSwapNode(struct usm_swap_dev * swapDevice){
    struct list_head * swap_node=NULL;
    pthread_mutex_lock(&swapDevice->devListLock);
    if (likely(!list_empty(&swapDevice->free_list))) {
        swap_node=swapDevice->free_list.next;
        list_del(swap_node);     // some .._init if some new list to be created later..
    }
    else
        printf("\tMayday, empty!\n");
    pthread_mutex_unlock(&swapDevice->devListLock);
    return swap_node;
}

void putSwapNode(struct usm_swap_dev * swapDevice, struct list_head * swapNode){
    pthread_mutex_lock(&swapDevice->devListLock);
    list_add_tail(swapNode,&swapDevice->free_list);
    pthread_mutex_unlock(&swapDevice->devListLock);
}

int s_os_write_page_to_swap(struct to_swap* node ) {
    if (usm_clear_and_set(node->proc, node->swapped_address, swap_value(node), 0)) {
        return 1;
    }
    if (fseeko((FILE *)node->swapDevice->backend, node->snode->offset, SEEK_SET)) {
        if(usm_set_pfn(node->proc,node->swapped_address,node->page->physicalAddress, 0))      // drop_swp_out/*_and_free*/...
            getchar();
        // restore_used_page(&((struct optEludeList *)(victimNode->page->usedListPositionPointer))->iulist);
        return 1;
    }
    if (unlikely(fwrite((void*)(node->page->data),SYS_PAGE_SIZE,1,(FILE *)node->swapDevice->backend)!=1)) {
        if(usm_set_pfn(node->proc,node->swapped_address,node->page->physicalAddress, 0))
            getchar();
        // restore_used_page(&((struct optEludeList *)(victimNode->page->usedListPositionPointer))->iulist);
        return 1;
    }

    return 0;
}

int s_os_read_page_from_swap(struct to_swap* node, struct page* usmPage) {
    if (fseeko((FILE *)node->swapDevice->backend, node->snode->offset, SEEK_SET)) {
        return 1;
    }
    if (unlikely(fread((void*)usmPage->data,SYS_PAGE_SIZE,1,(FILE *)node->swapDevice->backend)!=1)) {
        pthread_mutex_lock(&policiesSet1Swaplock);
        list_add(&node->globlist,&swapList);         // __
        pthread_mutex_unlock(&policiesSet1Swaplock);
        return 1;
    }

    return 0;
}

int swap_out(struct to_swap * node) {
    pthread_mutex_lock(&procDescList[node->proc].lock);
    list_add(&node->iulist, &procDescList[node->proc].swapCache);
    pthread_mutex_unlock(&procDescList[node->proc].lock);

    if (s_os_write_page_to_swap(node)) {
        putSwapNode(node->swapDevice,&node->snode->iulist);
        return 1;
    }

    list_del(&node->iulist);
    if (!node->retaken) {
        usmAddProcSwapNode(node);
        pthread_mutex_lock(&policiesSet1Swaplock);
        list_add(&node->globlist,&swapList);
        pthread_mutex_unlock(&policiesSet1Swaplock);
    } else {
        putSwapNode(node->swapDevice,&node->snode->iulist);

        return 0;
    }
    return 0;
}

int swap_in(struct to_swap * node) {
    if (fseeko((FILE *)node->swapDevice->backend, node->snode->offset, SEEK_SET)) {
       return 1;
    }

    if (unlikely(fread((void*)node->page->data,SYS_PAGE_SIZE,1,(FILE *)node->swapDevice->backend)!=1)) {
        pthread_mutex_lock(&policiesSet1Swaplock);
        list_add(&node->globlist,&swapList);
        pthread_mutex_unlock(&policiesSet1Swaplock);
        return 1;
    }

    return 0;
}


struct usm_swap_dev_ops swap_device_one_ops = {.swap_in=swap_in, .swap_out=swap_out};
struct usm_swap_dev swap_device_one = {.number=1};


struct usm_swap_dev * number_to_swap_dev_impl (int n) {
    trace("TRACE: entering sOS::API::number_to_swap_dev\n");
    trace("TRACE: exiting sOS::API::number_to_swap_dev\n");

    return &swap_device_one;
}

struct usm_swap_dev * pick_swap_device_impl (int proc) {
    trace("TRACE: entering sOS::API::pick_swap_device\n");
    trace("TRACE: exiting sOS::API::pick_swap_device\n");

    return &swap_device_one;
}

static inline int usm_swap_impl(struct usm_event* usmEvent, struct policy_function* policy) {
    trace("TRACE: entering sOS::API::usm_swap_impl\n");

    struct to_swap *node = event_to_swap_entry(usmEvent);

    if (!node) {
        trace("TRACE: exiting sOS::API::usm_swap_impl -- error\n");
        return 1;
    }

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    event->attached_process = node->proc;
    event->virtual_id = node->swapped_address;

    if (policy->select_phys_to_virtual(event)) {
        trace("TRACE: exiting sOS::API::swap_in -- error\n");
        return 1;
    }

    node->page = get_usm_page_from_paddr(event->physical_id);

    if (node->swapDevice->swap_dev_ops.swap_in(node)) {
        policy->on_invalid(event);

        trace("TRACE: exiting sOS::API::usm_swap_impl -- error\n");
        return 1;
    }

    usmEvent->paddr = node->page->physicalAddress;
    if (usmSubmitAllocEvent(usmEvent)) {
        trace("TRACE: exiting sOS::API::usm_swap_impl -- error\n");
        return 1;
    }

    free(event);

    trace("TRACE: exiting sOS::API::usm_swap_impl\n");
    return 0;
}

static inline int usm_free_impl(struct usm_event* usmEvent, struct policy_function* policy) {
    trace("TRACE: entering sOS::API::usm_swap_impl\n");

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    event->attached_process = usmEvent->origin;
    event->virtual_id = usmEvent->vaddr;
    event->virtual_nb = usmEvent->length;

    if (policy->on_yield(event)) {
        trace("TRACE: exiting sOS::API::usm_swap_impl -- error\n");
        return 1;
    }

    free(event);

    trace("TRACE: exiting sOS::API::usm_swap_impl\n");
    return 0;
}

void handle_evict_request_impl(struct usm_event* usmEvent) {
    trace("TRACE: entering sOS::API::handle_evict_request\n");

    // policy->on_yield(first_page)
    // device->swap_out()

    trace("TRACE: exiting sOS::API::handle_evict_request\n");
}

void check_swap_out_impl(struct usm_event* usmEvent) {
    trace("TRACE: entering sOS::API::check_swap_out\n");
    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    event->attached_process = usmEvent->origin;
    struct resource resource;
    struct to_swap* node = (struct to_swap *) malloc(sizeof(struct to_swap));

    for (unsigned long i = 0; i < nb_resources; i++) {
        resource = resourceList[i];

        if (!resource.virtualResource)
            continue;

        event->physical_id = resource.physicalId;

        if (!rr_policy_detail.functions->select_virtual_to_evict(event)) {
            struct usm_swap_dev* swapDev = pick_swap_device_impl(usmEvent->origin);

            if (swapDev) {
                struct page * page = get_usm_page_from_paddr(resource.physicalId)

                node->page = page;
                node->proc = page->process;
                node->swapDevice = swapDev;
                node->swapped_address = event->virtual_id;
                node->snode = list_entry(getSwapNode(swapDev), struct swap_node, iulist);
                node->snode->toSwapHolder = (void *)node;

                if (swapDev->swap_dev_ops.swap_in(node))
                    printf("Swapped in of %lu to %lu failed!\n", event->physical_id, event->virtual_id);
                else
                    printf("Swapped in of %lu to %lu.\n", event->physical_id, event->virtual_id);

            }
        }
    }

    free(node);
    free(event);

    // policy->select_virtual_to_evict()
    // policy->on_yield()
    // device->swap_out

    trace("TRACE: exiting sOS::API::check_swap_out\n");
}

void check_swap_in_impl(struct usm_event* usmEvent) {
    trace("TRACE: entering sOS::API::check_swap_in\n");

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    event->attached_process = usmEvent->origin;
    struct resource resource;
    struct to_swap* node = (struct to_swap *) malloc(sizeof(struct to_swap));

    for (unsigned long i = 0; i < nb_resources; i++) {
        resource = resourceList[i];

        if (resource.virtualResource)
            continue;

        event->physical_id = resource.physicalId;

        if (!rr_policy_detail.functions->select_virtual_to_load(event)) {
            struct usm_swap_dev* swapDev = pick_swap_device_impl(usmEvent->origin);

            if (swapDev) {
                struct page * page = get_usm_page_from_paddr(resource.physicalId)

                node->page = page;
                node->proc = page->process;
                node->swapDevice = swapDev;
                node->swapped_address = event->virtual_id;
                node->snode = list_entry(getSwapNode(swapDev), struct swap_node, iulist);
                node->snode->toSwapHolder = (void *)node;

                if (swapDev->swap_dev_ops.swap_in(node))
                    printf("Swapped in of %lu to %lu failed!\n", event->physical_id, event->virtual_id);
                else
                    printf("Swapped in of %lu to %lu.\n", event->physical_id, event->virtual_id);

            }
        }
    }

    free(node);
    free(event);

    trace("TRACE: exiting sOS::API::check_swap_in\n");
}

create_usm_bindings(policy1, policy1_detail)
create_usm_bindings(fifo, fifo_policy_detail)
create_usm_bindings(round_robin, rr_policy_detail)

int policy1_evict_setup(unsigned int pagesNumber) {
    trace("TRACE: entering sOS::API::evict_setup\n");

    register_policy(policy1, policy1_detail)
    register_policy(fifo, fifo_policy_detail)
    register_policy(round_robin, rr_policy_detail)

    usm_evict_fallback = &handle_evict_request_impl;
    do_cond_swap_out = &check_swap_out_impl;
    do_cond_swap_in = &check_swap_in_impl;

    choose_swap_device=&pick_swap_device_impl;
    deviceNumberToSwapDevice=&number_to_swap_dev_impl;

    swap_device_one.swap_dev_ops=swap_device_one_ops;
    char * swapFileName = (char *) malloc (85);
    strcat(swapFileName, "/tmp/usmSwapFile");
    swap_device_one.backend=fopen("/tmp/usmSwapFile","wb+");
    if(!swap_device_one.backend) {
        trace("TRACE: exiting sOS::API::evict_setup -- error can't open file\n");
        perror("Failed: ");
        return 1;
    }

    if (init_swap_device_nodes(&swap_device_one, (unsigned long)100*1024*1024)) {
        trace("TRACE: exiting sOS::API::evict_setup -- error can't init device\n");
        return 1;
    }

    trace("TRACE: exiting sOS::API::evict_setup\n");

    return 0;
}
void policy1_evict_exit() {
    trace("TRACE: entering sOS::API::evict_exit\n");
    fclose((FILE *)swap_device_one.backend);
    trace("TRACE: exiting sOS::API::evict_exit\n");
}

struct usm_ops dev_usm_swap_ops = {
        usm_setup:
        &policy1_evict_setup,
        usm_free:
        &policy1_evict_exit
};
