#ifndef _BROKER_H
#define _BROKER_H

#include <pthread.h>

#include "comm.h"

struct broker_state_t {
    pthread_t thread;

    struct comm_state_t *comm;
};

void broker_init(struct broker_state_t *broker, struct comm_state_t *comm);

void *borker_thread(struct broker_state_t *state);

#endif
