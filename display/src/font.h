#ifndef _FONT_H
#define _FONT_H

#include <stdint.h>

#include "draw.h"
#include "unicode.h"

struct glyph_t {
    uint8_t w, h;
    int8_t y0;
    uint16_t data_offset;
};

struct glyph_range_t {
    codepoint_t start;
    uint16_t count;
};

typedef uint8_t *font_data_t;

struct font_t {
    uint16_t glyph_count;
    uint8_t space_width;
    uint8_t height;
    font_data_t data;
    struct glyph_range_t *ranges;
    struct glyph_t glyphs[];
};

utf8_cstr_t font_draw_text(
    const struct font_t *font,
    const coord_int_t x0,
    const coord_int_t y0,
    const colour_t colour,
    utf8_cstr_t text);
utf8_cstr_t font_draw_text_ellipsis(
    const struct font_t *font,
    const coord_int_t x0,
    const coord_int_t y0,
    const colour_t colour,
    utf8_cstr_t text,
    const coord_int_t width);

void font_text_metrics(
    const struct font_t *font,
    utf8_cstr_t text,
    coord_int_t *width,
    coord_int_t *height,
    coord_int_t *depth);

#endif
