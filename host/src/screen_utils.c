#include "screen_utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

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

bool table_row_formatter_append_exv(
    struct table_row_formatter_t *const this,
    const colour_t fgcolour,
    const colour_t bgcolour,
    const table_column_alignment_t alignment,
    const char *fmt,
    va_list args)
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

    return table_row_formatter_appendv(this, fmt, args);
}

bool table_row_formatter_append_ex(
    struct table_row_formatter_t *const this,
    const colour_t fgcolour,
    const colour_t bgcolour,
    const table_column_alignment_t alignment,
    const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const bool result = table_row_formatter_append_exv(
        this,
        fgcolour,
        bgcolour,
        alignment,
        fmt,
        args
        );
    va_end(args);
    return result;
}


#ifdef MISSING_FMAXF_FMINF

inline float fmaxf(const float a, const float b)
{
    return (a > b ? a : b);
}

inline float fminf(const float a, const float b)
{
    return (a < b ? a : b);
}

#endif


static inline float mywrapf(float a, float b)
{
    float result = fmodf(a, b);
    if (result < 0) {
        result = b + result;
    }
    return result;
}


static colour_t rgbf_to_rgb16(const float rf, const float gf, const float bf)
{
    const colour_t r = ((colour_t)(rf*31)) & (0x1f);
    const colour_t g = ((colour_t)(gf*63)) & (0x3f);
    const colour_t b = ((colour_t)(bf*31)) & (0x1f);

    return (r << 11) | (g << 5) | b;
}


colour_t hsv_to_rgb(
    float h,
    const float s,
    const float v)
{
    if (s == 0) {
        const colour_t r_b = (colour_t)(v*31);
        const colour_t g = (colour_t)(v*63);
        return (r_b << 11) | (g << 5) | r_b;
    }

    h = mywrapf(h, M_PI*2.);
    float indexf;
    const float fractional = modff(h / (M_PI*2.f/6.f), &indexf);

    const int index = (int)indexf;

    const float p = v * (1.0f - s);
    const float q = v * (1.0f - (s * fractional));
    const float t = v * (1.0f - (s * (1.0f - fractional)));

    switch (index) {
    case 0:
    {
        return rgbf_to_rgb16(v, t, p);
    }
    case 1:
    {
        return rgbf_to_rgb16(q, v, p);
    }
    case 2:
    {
        return rgbf_to_rgb16(p, v, t);
    }
    case 3:
    {
        return rgbf_to_rgb16(p, q, v);
    }
    case 4:
    {
        return rgbf_to_rgb16(t, p, v);
    }
    case 5:
    {
        return rgbf_to_rgb16(v, p, q);
    }
    }

    return 0x0000;
}

uint8_t luminance(const colour_t colour)
{
    static const uint32_t rfactor = 0x1322d0e;
    static const uint32_t gfactor = 0x2591686;
    static const uint32_t bfactor = 0x74bc6a;

    uint32_t r = ((colour & 0xf800) >> 10) | 1;
    uint32_t g = ((colour & 0x07e0) >> 5);
    uint32_t b = ((colour & 0x001f) << 1) | 1;

    return ((r*rfactor + g*gfactor + b*bfactor) & 0xff000000) >> 24;
}


colour_t get_text_colour(const colour_t background)
{
    if (luminance(background) <= 175) {
        return 0xffff;
    } else {
        return 0x0000;
    }
}
