#include "heap.h"

#include <assert.h>
#include <stdio.h>

static inline intptr_t index_up(intptr_t index)
{
    // for index == 0, index_up must not be called
    assert(index > 0);

    const intptr_t result = (index-1) / 2;
    return (result >= 0 ? result : 0);
}

static inline intptr_t index_branch_left(intptr_t index)
{
    return (index*2)+1;
}

static inline intptr_t index_branch_right(intptr_t index)
{
    return (index+1)*2;
}

static inline void swap(
    struct array_t *array, intptr_t index_a, intptr_t index_b)
{
    // tmp_b = array[index_b]
    // array[index_b] = array[index_a]
    // array[index_a] = tmp_b
    array_set(
        array,
        index_a,
        array_set(
            array,
            index_b,
            array_get(
                array,
                index_a)));
}

static void min_heapify(struct heap_t *heap, intptr_t from_index)
{
    const intptr_t left_index = index_branch_left(from_index);
    const intptr_t right_index = index_branch_right(from_index);
    const intptr_t length = array_length(&heap->array);

    void *const node = array_get(&heap->array, from_index);
    void *const left_child = array_get(
        &heap->array, left_index);
    void *const right_child = array_get(
        &heap->array, right_index);

    intptr_t smallest_index = from_index;
    void *smallest = node;

    if ((left_index < length) && heap->less(left_child, smallest))
    {
        smallest_index = left_index;
        smallest = left_child;
    }

    if ((right_index < length) && heap->less(right_child, smallest))
    {
        smallest_index = right_index;
        smallest = right_child;
    }

    if (smallest_index != from_index)
    {
        swap(&heap->array, smallest_index, from_index);
        // tail recursion with void result
        min_heapify(heap, smallest_index);
    }
}

/* static void sift_up(struct heap_t *heap, intptr_t from_index) */
/* { */
/*     void *const object = array_get(&heap->array, from_index); */
/*     while (from_index != 0) { */
/*         const intptr_t parent_index = index_parent(from_index); */
/*         void *const parent = array_get(&heap->array, parent_index); */
/*         if (!heap->less(object, parent)) { */
/*             break; */
/*         } */

/*         array_set(&heap->array, parent_index, object); */
/*         array_set(&heap->array, from_index, parent); */
/*         from_index = parent_index; */
/*     } */
/* } */

void heap_delete(struct heap_t *heap, intptr_t array_index)
{
    // move the element of interest to the last index
    swap(&heap->array, array_length(&heap->array)-1, array_index);
    array_pop(&heap->array, -1);

    // let the element sift down until the heap is reconstructed
    min_heapify(heap, array_index);
}

void heap_free(struct heap_t *heap)
{
    array_free(&heap->array);
}

void *heap_get_max(struct heap_t *heap)
{
    return array_get(&heap->array, -1);
}

void *heap_get_min(struct heap_t *heap)
{
    return array_get(&heap->array, 0);
}

void heap_init(
    struct heap_t *heap,
    intptr_t initial_size,
    heap_less_t less)
{
    array_init(&heap->array, initial_size);
    heap->less = less;
}

void heap_insert(struct heap_t *heap, void *const object)
{
    intptr_t obj_index = array_append(&heap->array, object);
    while (obj_index != 0) {
        intptr_t parent_index = index_up(obj_index);
        void *const parent_obj = array_get(&heap->array, parent_index);
        if (!heap->less(object, parent_obj)) {
            break;
        }
        array_set(&heap->array, parent_index, object);
        array_set(&heap->array, obj_index, parent_obj);
        obj_index = parent_index;
    }
}

void *heap_pop_max(struct heap_t *heap)
{
    return array_pop(&heap->array, -1);
}

void *heap_pop_min(struct heap_t *heap)
{
    // remove last element and set it as first and use the previous
    // first element as result
    void *const result = array_set(
        &heap->array, 0,
        array_pop(&heap->array, -1));

    min_heapify(heap, 0);

    return result;
}
