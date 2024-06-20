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

    trace("TRACE: exiting sOS::API::evict_setup\n");

    return 0;
}
void policy1_evict_exit() {
    trace("TRACE: entering sOS::API::evict_exit\n");
    trace("TRACE: exiting sOS::API::evict_exit\n");
}

struct usm_ops dev_usm_swap_ops = {
        usm_setup:
        &policy1_evict_setup,
        usm_free:
        &policy1_evict_exit
};
