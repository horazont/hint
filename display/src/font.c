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

utf8_cstr_t font_draw_text(
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
    return utf8_get_ptr(&ctx);
}

codepoint_t font_draw_text_ellipsis_take_care(
    const struct font_t *font,
    utf8_ctx_t *ctx,
    const coord_int_t x0,
    const coord_int_t xoffs0,
    const coord_int_t width,
    codepoint_t ch,
    const struct glyph_t *ellipsis,
    const coord_int_t y0,
    const colour_t colour)
{
    coord_int_t xoffs = xoffs0;
    int bufidx = 0;
    const struct glyph_t *glyphbuf[8];

    do {
        if (xoffs > width) {
            break;
        }
        if (ch == 0x20) {
            // we use NULL as hint for space for now -- we might have
            // to do this differently in the future!
            glyphbuf[bufidx++] = NULL;
            xoffs += font->space_width;
            continue;
        }
        glyphbuf[bufidx] = font_find_glyph(font, ch);
        if (!glyphbuf[bufidx]) {
            continue;
        }
        xoffs += glyphbuf[bufidx]->w;
        bufidx++;
        if (bufidx == 8) {
            // this is strange and well, cannot do anything about that
            // zero-width spaces?
            // we just assume it does not fit.
            xoffs = width+1;
            break;
        }
    } while ((ch = utf8_next(ctx)));

    if (xoffs > width) {
        glyphbuf[0] = ellipsis;
        bufidx = 1;
    }

    xoffs = xoffs0;
    for (int i = 0; i < bufidx; i++) {
        const struct glyph_t *glyph = glyphbuf[i];
        if (!glyph) {
            xoffs += font->space_width;
            continue;
        }
        const uint8_t *bitmap = &font->data[glyph->data_offset];
        draw_bitmap_transparent(
            x0 + xoffs, y0 - glyph->y0,
            glyph->w, glyph->h,
            colour, bitmap);
        xoffs += glyph->w;
    }

    return ch;
}

utf8_cstr_t font_draw_text_ellipsis(
    const struct font_t *font,
    const coord_int_t x0,
    const coord_int_t y0,
    const colour_t colour,
    utf8_cstr_t text,
    const coord_int_t width)
{
    const struct glyph_t *ellipsis = font_find_glyph(
        font, CODEPOINT_ELLIPSIS);

    if (!ellipsis) {
        // no point in drawing with ellipsis without ellipsis
        return font_draw_text(font, x0, y0, colour, text);
    }

    coord_int_t y = y0;
    coord_int_t xoffs = 0;
    utf8_ctx_t ctx;
    codepoint_t ch;
    utf8_init(&ctx, text);


    for (ch = utf8_next(&ctx);
         ch != 0;
         ch = utf8_next(&ctx))
    {
        if (ch == 0x20) {
            if ((width - (xoffs+font->space_width)) < ellipsis->w) {
                ch = font_draw_text_ellipsis_take_care(
                    font, &ctx, x0, xoffs, width, ch, ellipsis, y0, colour);
                break;
            }
            xoffs += font->space_width;
            continue;
        }

        const struct glyph_t *glyph = font_find_glyph(font, ch);
        if (glyph == NULL) {
            continue;
        }
        if ((width - (xoffs+glyph->w)) < ellipsis->w) {
            ch = font_draw_text_ellipsis_take_care(
                font, &ctx, x0, xoffs, width, ch, ellipsis, y0, colour);
            break;
        }

        const uint8_t *bitmap = &font->data[glyph->data_offset];
        draw_bitmap_transparent(
            x0 + xoffs, y-glyph->y0,
            glyph->w, glyph->h,
            colour,
            bitmap);
        xoffs += glyph->w;
    }

    while (ch != 0) {
        ch = utf8_next(&ctx);
    }
    return utf8_get_ptr(&ctx);
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
