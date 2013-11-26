#include "buffer.h"

#include "utils.h"

static uint16_t buffer_offset VAR_RAM = 0;
static uint8_t buffer[BUFFER_SIZE] VAR_RAM __attribute__((aligned(4)));

void *buffer_alloc(const uint16_t length)
{
    if (BUFFER_SIZE - buffer_offset < length) {
        return NULL;
    }

    void *result = &buffer[buffer_offset];
    buffer_offset += length;
    // make sure all results are 32bit aligned
    buffer_offset += 4-(buffer_offset%4);

    return result;
}

void buffer_release_all()
{
    buffer_offset = 0;
}
