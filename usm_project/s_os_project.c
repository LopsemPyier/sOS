#include "s_os_project.h"

int VIRTUAL_RESOURCE_SIZE = 0;

int main(int argc, char* argv[]) {
    printf("TRACE: entering main");

    VIRTUAL_RESOURCE_SIZE = SYS_PAGE_SIZE;

    usm_parse_args(argv, argc);

    struct usm_global_ops global_ops={
        .dev_usm_alloc_ops=&dev_usm_ops,
        .dev_usm_swap_ops=&dev_usm_swap_ops,
        //.oom_ops=&dev_usm_oom_ops
    };



    usm_launch(global_ops);
}

