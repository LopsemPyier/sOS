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
    if (fseeko((FILE *)victimNode->swapDevice->backend, victimNode->snode->offset, SEEK_SET)) {
        if(usm_set_pfn(victimNode->proc,victimNode->swapped_address,victimNode->page->physicalAddress, 0))      // drop_swp_out/*_and_free*/...
            getchar();
        // restore_used_page(&((struct optEludeList *)(victimNode->page->usedListPositionPointer))->iulist);
        return 1;
    }
    if (unlikely(fwrite((void*)(victimNode->page->data),SYS_PAGE_SIZE,1,(FILE *)victimNode->swapDevice->backend)!=1)) {
        if(usm_set_pfn(victimNode->proc,victimNode->swapped_address,victimNode->page->physicalAddress, 0))
            getchar();
        // restore_used_page(&((struct optEludeList *)(victimNode->page->usedListPositionPointer))->iulist);
        return 1;
    }

    return 0;
}

int s_os_read_page_from_swap(struct to_swap* node, struct page* usmPage) {
    if (fseeko((FILE *)luckyNode->swapDevice->backend, luckyNode->snode->offset, SEEK_SET)) {
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
    }

    list_del(&node->iulist);
    if (!node->retaken) {
        usmAddProcSwapNode(node);
        pthread_mutex_lock(&policiesSet1Swaplock);
        list_add(&node->globlist,&swapList);
        pthread_mutex_unlock(&policiesSet1Swaplock);
    } else {
        putSwapNode(victimNode->swapDevice,&victimNode->snode->iulist);

        return 0;
    }
    return 0;
}

int swap_in(struct to_swap * node) {



    return 0;
}


struct usm_swap_dev_ops swap_device_one_ops = {.swap_in=swap_in, .swap_out=swap_out};
struct usm_swap_dev swap_device_one = {.number=1};


static inline int usm_swap_impl(struct usm_event* usmEvent, struct policy_function* policy) {
    trace("TRACE: entering sOS::API::usm_swap_impl\n");

    struct sOSEvent *event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

    event->attached_process = usmEvent->origin;
    event->virtual_id = usmEvent->vaddr;
    event->virtual_nb = usmEvent->length;

    if (policy->select_virtual_to_evict(event)) {
        trace("TRACE: exiting sOS::API::usm_swap_impl -- error\n");
        return 1;
    }

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

    trace("TRACE: exiting sOS::API::usm_swap_impl\n");
    return 0;
}

void handle_evict_request_impl(struct usm_event* usmEvent) {
    trace("TRACE: entering sOS::API::handle_evict_request\n");
    trace("TRACE: exiting sOS::API::handle_evict_request\n");
}

void check_swap_out_impl(struct usm_event* usmEvent) {
    trace("TRACE: entering sOS::API::check_swap_out\n");
    trace("TRACE: exiting sOS::API::check_swap_out\n");
}

void check_swap_in_impl(struct usm_event* usmEvent) {
    trace("TRACE: entering sOS::API::check_swap_in\n");
    trace("TRACE: exiting sOS::API::check_swap_in\n");
}

struct usm_swap_dev * number_to_swap_dev_impl (int n) {
    trace("TRACE: entering sOS::API::number_to_swap_dev\n");
    trace("TRACE: exiting sOS::API::number_to_swap_dev\n");

    return NULL;
}

struct usm_swap_dev * pick_swap_device_impl (int proc) {
    trace("TRACE: entering sOS::API::pick_swap_device\n");
    trace("TRACE: exiting sOS::API::pick_swap_device\n");

    return NULL;
}

create_usm_bindings(policy1, policy1_detail)

int policy1_evict_setup(unsigned int pagesNumber) {
    trace("TRACE: entering sOS::API::evict_setup\n");

    register_policy(policy1, policy1_detail)

    usm_evict_fallback = &handle_evict_request_impl;
    do_cond_swap_out = &check_swap_out_impl;
    do_cond_swap_in = &check_swap_in_impl;

    choose_swap_device=&pick_swap_device_impl;
    deviceNumberToSwapDevice=&number_to_swap_dev_impl;

    swap_device_one.swap_dev_ops=swap_device_one_ops;
    char * swapFileName = (char *) malloc (85);
    strcat(swapFileName, "/tmp/usmSwapFile");
    swap_device_one.backend=fopen(swapFileName,"wb+");
    if(!swap_device_one.backend) {
        trace("TRACE: exiting sOS::API::evict_setup -- error\n");
        return 1;
    }

    if (init_swap_device_nodes(&swap_device_one, (unsigned long)100*1024*1024)) {
        trace("TRACE: exiting sOS::API::evict_setup -- error\n");
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
