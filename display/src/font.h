#ifndef _FONT_H
#define _FONT_H

#include <stdint.h>

typedef uint16_t wchar_t;

struct glyph_t {
    uint16_t codepoint;
    uint8_t w, h;
    int8_t y0;
    uint16_t data_offset;
};

typedef uint8_t font_data_t[];

struct font_t {
    uint16_t glyph_count;
    uint8_t space_width;
    font_data_t *data;
    struct glyph_t glyphs[];
};

void font_draw_text(
    const struct font_t *font,
    const wchar_t *text,
    const int textlen);

#endif
