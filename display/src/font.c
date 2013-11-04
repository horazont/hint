#include "font.h"

#include <stdlib.h>

inline const struct glyph_t *font_find_glyph(const struct font_t *font,
                                             const codepoint_t codepoint)
{
    uint16_t offset = 0;

    for (const struct glyph_range_t *range = font->ranges;
         range->count > 0;
         range++)
    {
        codepoint_t end = range->start + range->count - 1;
        if ((codepoint >= range->start) && (codepoint <= end)) {
            return &font->glyphs[offset + (codepoint - range->start)];
        }

        offset += range->count;
    }

    return NULL;
}

void font_draw_text(
    const struct font_t *font,
    const coord_int_t x0,
    const coord_int_t y0,
    const colour_t colour,
    utf8_cstr_t text)
{
    coord_int_t x = x0, y = y0;
    utf8_ctx_t ctx;
    utf8_init(&ctx, text);

    for (codepoint_t ch = utf8_next(&ctx);
         ch != 0;
         ch = utf8_next(&ctx))
    {
        if (ch == 0x20) {
            x += font->space_width;
            continue;
        }

        const struct glyph_t *glyph = font_find_glyph(font, ch);
        if (glyph == NULL) {
            continue;
        }

        const uint8_t *bitmap = &font->data[glyph->data_offset];
        draw_bitmap_transparent(
            x, y-glyph->y0,
            glyph->w, glyph->h,
            colour,
            bitmap);
        x += glyph->w;
    }
}

inline coord_int_t max(const coord_int_t a, const coord_int_t b)
{
    return (a > b ? a : b);
}

inline coord_int_t min(const coord_int_t a, const coord_int_t b)
{
    return (a < b ? a : b);
}

void font_text_metrics(
    const struct font_t *font,
    utf8_cstr_t text,
    coord_int_t *width,
    coord_int_t *height,
    coord_int_t *depth)
{
    coord_int_t w = 0, h = 0, d = 0;
    utf8_ctx_t ctx;
    utf8_init(&ctx, text);

    for (codepoint_t ch = utf8_next(&ctx);
         ch != 0;
         ch = utf8_next(&ctx))
    {
        if (ch == 0x20) {
            w += font->space_width;
            continue;
        }

        const struct glyph_t *glyph = font_find_glyph(font, ch);
        if (glyph == NULL) {
            continue;
        }
        h = max(h, glyph->y0);
        d = max(d, max(0, glyph->h - glyph->y0));
    }

    *width = w;
    *height = h;
    *depth = d;
}
