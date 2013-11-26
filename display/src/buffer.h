#ifndef _BUFFER_H
#define _BUFFER_H

#include <stdint.h>

#define BUFFER_SIZE (1024)

/**
 * Allocate a piece of the dynamic memory buffer.
 *
 * A buffer allocated this way is valid until all dynamic buffers are
 * flushed using buffer_release_all().
 *
 * @param length amount of bytes to allocate. For aligment reasons, the
 *        length is rounded up to the next multiple of four.
 * @return pointer to the buffer, or NULL if not enough space is left
 *         in the dynamic memory heap
 */
void *buffer_alloc(const uint16_t length);

/**
 * Release and invalidate all pointers acquired through buffer_alloc().
 */
void buffer_release_all();

#endif
