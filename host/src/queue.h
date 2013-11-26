#ifndef _QUEUE_H
#define _QUEUE_H

#include <pthread.h>
#include <stdbool.h>

struct queue_item_t {
    void *data;
    struct queue_item_t *next;
};

struct queue_t {
    pthread_mutex_t mutex;
    struct queue_item_t *head;
    struct queue_item_t *tail;
};

bool queue_empty(struct queue_t *queue);
void queue_free(struct queue_t *queue);
void queue_init(struct queue_t *queue);
void *queue_pop(struct queue_t *queue);
void queue_push(struct queue_t *queue, void *data);
void queue_push_front(struct queue_t *queue, void *data);

#endif
