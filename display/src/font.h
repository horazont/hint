#ifndef _FONT_H
#define _FONT_H

#include <stdint.h>

#include "draw.h"
#include "unicode.h"

struct glyph_t {
    codepoint_t codepoint;
    uint8_t w, h;
    int8_t y0;
    uint16_t data_offset;
};

typedef uint8_t *font_data_t;

struct font_t {
    uint16_t glyph_count;
    uint8_t space_width;
    font_data_t data;
    struct glyph_t glyphs[];
};

void font_draw_text(
    const struct font_t *font,
    const coord_int_t x0,
    const coord_int_t y0,
    const colour_t colour,
    const utf8_str_t text);

void font_text_metrics(
    const struct font_t *font,
    const utf8_str_t text,
    coord_int_t *width,
    coord_int_t *height,
    coord_int_t *depth);

#endif
