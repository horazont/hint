#ifndef _BROKER_H
#define _BROKER_H

#include <pthread.h>

#include "comm.h"

struct broker_t {
    pthread_t thread;

    struct comm_t *comm;
};

void broker_init(struct broker_t *broker, struct comm_t *comm);

void broker_process_message(void *item);

void *broker_thread(struct broker_t *state);

#endif
