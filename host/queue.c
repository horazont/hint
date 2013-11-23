#include "queue.h"

#include <stdio.h>
#include <stdlib.h>

bool queue_empty(struct queue_t *queue)
{
    bool result = false;
    pthread_mutex_lock(&queue->mutex);
    result = (queue->head == NULL);
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

void queue_free(struct queue_t *queue)
{
    if (!queue_empty(queue)) {
        fprintf(stderr, "queue: non-empty queue destroyed. this is a memory leak\n");
    }

    pthread_mutex_lock(&queue->mutex);
    struct queue_item_t *item = queue->head;
    while (item) {
        struct queue_item_t *next = item->next;
        free(item);
        item = next;
    }
    pthread_mutex_unlock(&queue->mutex);
    pthread_mutex_destroy(&queue->mutex);
    queue->head = NULL;
    queue->tail = NULL;
}

void queue_init(struct queue_t *queue)
{
    pthread_mutex_init(&queue->mutex, NULL);
    queue->head = NULL;
    queue->tail = NULL;
}

void *queue_pop(struct queue_t *queue)
{
    pthread_mutex_lock(&queue->mutex);
    if (queue->head == NULL) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    struct queue_item_t *item = queue->head;
    void *result = item->data;
    queue->head = item->next;
    free(item);
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

void queue_push(struct queue_t *queue, void *data)
{
    struct queue_item_t *item = malloc(sizeof(struct queue_item_t));
    item->data = data;
    item->next = NULL;

    pthread_mutex_lock(&queue->mutex);
    if (queue->tail == NULL) {
        queue->head = item;
        queue->tail = item;
    } else {
        queue->tail->next = item;
        queue->tail = item;
    }
    pthread_mutex_unlock(&queue->mutex);
}

void queue_push_front(struct queue_t *queue, void *data)
{
    struct queue_item_t *item = malloc(sizeof(struct queue_item_t));
    item->data = data;
    item->next = NULL;

    pthread_mutex_lock(&queue->mutex);
    if (queue->tail == NULL) {
        queue->head = item;
        queue->tail = item;
    } else {
        item->next = queue->head;
        queue->head = item;
    }
    pthread_mutex_unlock(&queue->mutex);

}
