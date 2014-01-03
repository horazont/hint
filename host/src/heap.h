#ifndef _SORTED_ARRAY_H
#define _SORTED_ARRAY_H

#include "array.h"

typedef bool (*heap_less_t)(void *const a, void *const b);

struct heap_t {
    struct array_t array;
    heap_less_t less;
};

void heap_delete(struct heap_t *heap, intptr_t array_index);
void heap_free(struct heap_t *heap);
void *heap_get_max(struct heap_t *heap);
void *heap_get_min(struct heap_t *heap);
void heap_init(
    struct heap_t *heap,
    intptr_t initial_size,
    heap_less_t less);

static inline intptr_t heap_length(struct heap_t *heap)
{
    return array_length(&heap->array);
}

void heap_insert(struct heap_t *heap, void *const object);
void *heap_pop_max(struct heap_t *heap);
void *heap_pop_min(struct heap_t *heap);

#endif
