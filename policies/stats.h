//
// Created by nathan on 05/07/24.
//

#ifndef SOS_STATS_H
#define SOS_STATS_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <limits.h>
#include <sys/time.h>
#include "queues.h"


extern void init_stats_queues();
extern pthread_mutex_t stat_lock;
extern void store_queues();


#endif //SOS_STATS_H
