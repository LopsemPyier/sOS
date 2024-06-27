
#ifndef S_OS_PROJECT_H
#define S_OS_PROJECT_H

#include "../../include/usm/usm.h"
#include "policies/usm_linking.h"

static inline struct page * get_usm_page_from_paddr(unsigned long paddr) {
    if(unlikely(usmPfnToPageIndex(paddr) >= globalPagesNumber || paddr < basePFN)) {
#ifdef DEBUG
        printf("[Sys] Event to page result uncoherent\n");
#endif
        return NULL;
    }
    return pagesList+(paddr-basePFN);
}


extern int commitEvent(struct sOSEvent *event);

#endif