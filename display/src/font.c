#include "font.h"

#include <stdlib.h>

inline const struct glyph_t *font_find_glyph(const struct font_t *font,
                                             const codepoint_t codepoint)
{
    const struct glyph_t *result = NULL;
    for (wchar_t i = 0; i < font->glyph_count; i++) {
        result = &font->glyphs[i];
        if (result->codepoint == codepoint) {
            return result;
        }
    }
    return NULL;
}

void font_draw_text(
    const struct font_t *font,
    const coord_int_t x0,
    const coord_int_t y0,
    const colour_t colour,
    const codepoint_t *text,
    const int textlen)
{
    coord_int_t x = x0, y = y0;
    const codepoint_t *ch = text;
    for (int i = 0; i < textlen; i++) {
        if (*ch == 0x20) {
            x += font->space_width;
            ch++;
            continue;
        }

        const struct glyph_t *glyph = font_find_glyph(font, *ch);
        if (glyph == NULL) {
            ch++;
            continue;
        }

        const uint8_t *bitmap = &font->data[glyph->data_offset];
        draw_bitmap_transparent(
            x, y-glyph->y0,
            glyph->w, glyph->h,
            colour,
            bitmap);
        x += glyph->w;
        ch++;
    }
}
