#ifndef _BROKER_H
#define _BROKER_H

#include <pthread.h>

#include "comm.h"
#include "screen.h"

#define SCREEN_COUNT                    (2)
#define SCREEN_BUS_MONITOR              (0)
#define SCREEN_WEATHER_INFO             (1)
#define SCREEN_MISC                     (2)

#define CLOCK_UPDATE_INTERVAL           (1000)

struct broker_t;

typedef bool(*task_func_t)(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata);

struct task_t {
    task_func_t func;
    struct timespec run_at;
    void *userdata;
};

struct broker_t {
    pthread_t thread;

    struct comm_t *comm;

    bool touch_is_up;
    struct screen_t screens[SCREEN_COUNT];
    int active_screen;

    struct array_t tasks;
};

void broker_enqueue_new_task_at(
    struct broker_t *broker,
    task_func_t func,
    const struct timespec *run_at,
    void *userdata);
void broker_enqueue_new_task_in(
    struct broker_t *broker,
    task_func_t func,
    uint32_t in_msec,
    void *userdata);
void broker_enqueue_task(struct broker_t *broker, struct task_t *task);


void broker_init(struct broker_t *broker, struct comm_t *comm);

void *broker_thread(struct broker_t *state);

#endif
