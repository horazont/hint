#include "screen_utils.h"

#include <stdarg.h>
#include <stdio.h>

#include "utils.h"

bool table_row_formatter_appendv(
    struct table_row_formatter_t *const this,
    const char *fmt,
    va_list args)
{
    const size_t length = this->buflen-this->offset;
    int written = vsnprintf(
        &this->buffer[this->offset],
        length,
        fmt,
        args)+1; // <- note the +1 here, counting the zero byte!
    if ((unsigned int)written >= length) {
        if (!this->dynamic) {
            return false;
        }
        size_t newsize = this->offset+written+1;
        char *newbuf = realloc(this->buffer, newsize);
        if (!newbuf) {
            free(this->buffer);
            panicf("table_row_formatter: out of memory\n");
        }
        this->buffer = newbuf;
        this->buflen = newsize;
        return table_row_formatter_appendv(this, fmt, args);
    }

    this->offset += written;
    return true;
}

bool table_row_formatter_append(
    struct table_row_formatter_t *const this,
    const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    bool result = table_row_formatter_appendv(this, fmt, args);
    va_end(args);
    return result;
}

void table_row_formatter_free(
    struct table_row_formatter_t *const this)
{
    if (this->dynamic) {
        free(this->buffer);
        this->buffer = NULL;
        this->buflen = 0;
    }
}

char *table_row_formatter_get(
    struct table_row_formatter_t *const this,
    size_t *length)
{
    if (length) {
        *length = this->offset;
    }
    return this->buffer;
}

void table_row_formatter_init(
    struct table_row_formatter_t *const this,
    char *const buffer,
    size_t length)
{
    this->buffer = buffer;
    this->buflen = length;
    this->offset = 0;
    this->dynamic = false;
}

void table_row_formatter_init_dynamic(
    struct table_row_formatter_t *const this,
    size_t initial_size)
{
    if (initial_size > 0) {
        this->buffer = malloc(initial_size);
        if (!this->buffer) {
            panicf("table_row_formatter: out of memory\n");
        }
        this->buflen = initial_size;
    } else {
        this->buffer = NULL;
        this->buflen = 0;
    }

    this->offset = 0;
    this->dynamic = false;
}

void table_row_formatter_reset(
    struct table_row_formatter_t *const this)
{
    this->offset = 0;
}
