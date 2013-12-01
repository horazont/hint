#include "array.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

bool array_empty(struct array_t *array)
{
    return array->length == 0;
}

void array_free(struct array_t *array)
{
    free(array->ptrs);
    array->ptrs = NULL;
    array->length = 0;
    array->size = 0;
}

void *array_get(const struct array_t *array, intptr_t idx)
{
    if (idx < 0) {
        idx = array->length + idx;
    }
    return array->ptrs[idx];
}

void array_grow(struct array_t *array)
{
    intptr_t new_size = array->size * 2;
    intptr_t new_bytes = new_size * sizeof(void*);
    void **new_buffer = realloc(array->ptrs, new_bytes);
    if (!new_buffer) {
        fprintf(stderr, "array_grow: out of memory!\n");
        assert(new_buffer);
    }
    array->ptrs = new_buffer;
    array->size = new_size;
}

void array_init(struct array_t *array, intptr_t initial_size)
{
    assert(initial_size >= 0);
    array->length = 0;
    array->size = initial_size;
    array->ptrs = malloc(sizeof(void*)*initial_size);
}

intptr_t array_length(const struct array_t *array)
{
    return array->length;
}

void *array_pop(struct array_t *array, intptr_t idx)
{
    if (idx < 0) {
        return array_pop(array, array_length(array)+idx);
    }

    void *result = array->ptrs[idx];
    memmove(&array->ptrs[idx], &array->ptrs[idx+1], array_length(array)-(idx+1));
    array->length -= 1;
    return result;
}

intptr_t array_push(struct array_t *array, intptr_t idx, void *dataptr)
{
    intptr_t len = array_length(array);
    if (idx < 0) {
        idx = len + idx;
    } else if (idx > len) {
        idx = len;
    }

    if (len == array->size) {
        array_grow(array);
    }

    if (idx < len) {
        memmove(&array->ptrs[idx+1], &array->ptrs[idx], len-idx);
    }
    array->ptrs[idx] = dataptr;
    array->length += 1;
    return idx;
}

void *array_set(struct array_t *array, intptr_t idx, void *dataptr)
{
    if (idx < 0) {
        idx = array->length + idx;
    }
    void *result = array->ptrs[idx];
    array->ptrs[idx] = dataptr;
    return result;
}
