//
// Created by nathan on 18/06/24.
//

#ifndef SOS_GHOST_LINKING_H
#define SOS_GHOST_LINKING_H

#include "api.h"
#include "policies.h"

extern struct policy_detail* ghost_policy;

void initResources(unsigned long resourceSize);
void add_cpu(unsigned long id);

#endif //SOS_GHOST_LINKING_H
