#ifndef _ARRAY_H
#define _ARRAY_H

#include <stdint.h>
#include <stdbool.h>

struct array_t {
    intptr_t length;
    intptr_t size;
    void **ptrs;
};

/**
 * Check whether the array is empty.
 *
 * @return true if no elements are in the array, false otherwise.
 */
bool array_empty(struct array_t *array);

/**
 * Release all resources allocated by the array.
 *
 * This does not free any values stored inside the array.
 */
void array_free(struct array_t *array);

/**
 * Return an element from the array without removing it.
 *
 * @param idx Index of the element. This must be in the range
 *        [-length..length-1]. If the index is negative, the length of
 *        the array is added to it before accessing the array. Thus,
 *        an index of -1 can be used to access the last element of the
 *        array.
 *
 * @return the value stored at the given position inside the array.
 */
void *array_get(const struct array_t *array, intptr_t idx);

/**
 * Allocate more storage for array elements.
 */
void array_grow(struct array_t *array);

/**
 * Initialize an array. The amount of records allocated initially is
 * controlled by initial_size.
 *
 * @param initial_size how much space shall be allocated for elements.
 *        If you know beforehands how many elements the array will
 *        store, it is wise to pass this to the constructor to reduce
 *        memory fragmentation and improve performance during inserts.
 */
void array_init(struct array_t *array, intptr_t initial_size);

/**
 * Obtain the length of the array. The length is not neccessariliy
 * equal to the amount of storage allocated, but reflects how many
 * elements are actually stored in the array.
 *
 * @return count of items stored in the array
 */
intptr_t array_length(const struct array_t *array);

/**
 * Remove and return an element from the array.
 *
 * @param idx The index of the element. The semantics of the idx value
 *        are the same as in array_get.
 */
void *array_pop(struct array_t *array, intptr_t idx);

/**
 * Insert an element to the array.
 *
 * @param idx The index of the new element. THis is clamped to the
 *        length of the array, otherwise the semantics are the same as
 *        for array_get. Thus, to insert an element to the end of the
 *        array, use array_push(array, SIZE_MAX, some_data). To add
 *        an element at the second-last position, use
 *        array_push(array, -1, some_data).
 * @return the new absolute index of the element inserted
 */
intptr_t array_push(struct array_t *array, intptr_t idx, void *dataptr);

/**
 * Exchange an element inplace.
 *
 * @param idx The index of the element to exchange. Semantics are the
 *        same as for array_get.
 * @return the old element
 */
void *array_set(struct array_t *array, intptr_t idx, void *dataptr);

#endif
