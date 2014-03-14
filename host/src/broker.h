#ifndef _BROKER_H
#define _BROKER_H

#include <pthread.h>

#include "comm.h"
#include "screen.h"
#include "xmppintf.h"
#include "heap.h"
#include "sensor.h"

#define SCREEN_COUNT                    (4)
#define SCREEN_BUS_MONITOR              (0)
#define SCREEN_WEATHER_INFO             (1)
#define SCREEN_NET                      (2)
#define SCREEN_MISC                     (3)

#define CLOCK_UPDATE_INTERVAL           (1000)
#define SLEEPOUT_TIMER                  (60000)
#define SLEEPOUT_TIMER_INTERVAL         (5000)

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
    struct xmpp_t *xmpp;

    bool touch_is_up;
    bool terminated;
    pthread_mutex_t activity_mutex;
    bool asleep;
    struct timespec last_activity;
    pthread_mutex_t screen_mutex;
    struct screen_t screens[SCREEN_COUNT];
    int active_screen;

    struct heap_t tasks;

    struct {
        pthread_mutex_t mutex;
        struct array_t all_batches;
        // we need a heap, because we must submit batches in the order of their
        // timestamps
        struct heap_t full_batches;
        struct array_t free_batches;
        struct sensor_readout_batch_t *curr_batch;
    } sensor;
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

void broker_free(
    struct broker_t *broker);

void broker_init(
    struct broker_t *broker,
    struct comm_t *comm,
    struct xmpp_t *xmpp);

void broker_remove_task_func(
    struct broker_t *broker,
    task_func_t func);

void *broker_thread(struct broker_t *state);

#endif
