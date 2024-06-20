
#ifndef ALLOC_LINKED_API_H
#define ALLOC_LINKED_API_H

#include "../../s_os_project.h"


struct optEventList {
    struct usm_event * event;      // no pointer needed..
    struct list_head iulist;
    struct list_head proclist;
};

extern struct list_head eventList;


int submitResourceList(struct sOSEvent* event, struct list_head* resource_to_submit) {
    struct optEventList* node = list_entry(event->eventPosition, struct optEventList, iulist);
    struct usm_event* usmEvent = node->event;
    list_del(event->eventPosition);
    free(node);

    usmEvent->paddr = event->physical_id;

    struct optEludeList* resource_iter;
    int vpfn = -1, count = 0, pfn, pos = 0, pages = 0;
    bool first = true;
    list_for_each_entry(resource_iter, resource_to_submit, iulist) {
        pages++;
        if (first) {
            first = false;
            vpfn = resource_iter->resource->physicalId;
            usmPrepPreAlloc(usmEvent, vpfn++, pos);
        } else {
            pfn = resource_iter->resource->physicalId;

            if (likely(vpfn == pfn)) {
                count++;
            } else {
                usmPrepPreAllocFastCounted(usmEvent, pos, count);
                usmPrepPreAlloc(usmEvent, pfn, ++pos);
                count = 0;
            }
            vpfn = pfn + 1;
        }
    }
    if (likely(count)) {
        usmPrepPreAllocFastCounted(usmEvent, pos, count);
    }

    usmEvent->length = event->virtual_nb;
    int ret = usmSubmitAllocEvent(usmEvent);
    free(event);
    return ret;
}




#endif