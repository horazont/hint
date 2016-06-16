#include "screen_utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"

bool _table_row_formatter_extend(
    struct table_row_formatter_t *const this,
    size_t min_size
    )
{
    if (min_size <= this->buflen) {
        return true;
    }

    if (!this->dynamic) {
        return false;
    }

    char *newbuf = realloc(this->buffer, min_size);
    if (!newbuf) {
        free(this->buffer);
        panicf("table_row_formatter: out of memory\n");
        return false;
    }

    this->buffer = newbuf;
    this->buflen = min_size;

    return true;
}

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
        if (!_table_row_formatter_extend(this, this->offset+written+1)) {
            return false;
        }
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
    this->dynamic = true;
}

void table_row_formatter_reset(
    struct table_row_formatter_t *const this)
{
    this->offset = 0;
}

bool table_row_formatter_append_ex(
    struct table_row_formatter_t *const this,
    const colour_t fgcolour,
    const colour_t bgcolour,
    const table_column_alignment_t alignment,
    const char *fmt, ...)
{
    const size_t min_additional = sizeof(colour_t)*2+sizeof(table_column_alignment_t)+1;
    if (this->offset + min_additional > this->buflen) {
        if (!_table_row_formatter_extend(this, this->buflen + min_additional*2)) {
            return false;
        }
    }

    memcpy(&this->buffer[this->offset], &bgcolour, sizeof(colour_t));
    this->offset += sizeof(colour_t);
    memcpy(&this->buffer[this->offset], &fgcolour, sizeof(colour_t));
    this->offset += sizeof(colour_t);
    memcpy(&this->buffer[this->offset], &alignment, sizeof(table_column_alignment_t));
    this->offset += sizeof(table_column_alignment_t);

    va_list args;
    va_start(args, fmt);
    bool result = table_row_formatter_appendv(this, fmt, args);
    va_end(args);

    return result;
}
