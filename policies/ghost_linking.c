#include "ghost_linking.h"

int VIRTUAL_RESOURCE_SIZE = 1;
char const *filename = "scheduler_rr_stats.csv";

int submitResourceList(struct sOSEvent* event, struct list_head* resource_to_submit) {
    return 0;
}

struct resource* resourceList = NULL;
