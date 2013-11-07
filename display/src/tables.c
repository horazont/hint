#include "tables.h"

void table_init(
    struct table_t *ctx,
    const struct table_column_t *columns,
    const uint8_t column_count,
    const coord_int_t row_height)
{
    ctx->columns = columns;
    ctx->column_count = column_count;
    ctx->row_height = row_height;
    ctx->x0 = 0;
    ctx->row_offset = 0;
}

void table_start(
    struct table_t *ctx,
    const coord_int_t x0,
    const coord_int_t y0)
{
    ctx->x0 = x0;
    ctx->row_offset = y0;
}

void table_row(
    struct table_t *ctx,
    const struct font_t *font,
    const utf8_cstr_t *columns,
    const colour_t text_colour)
{
    coord_int_t x = ctx->x0;
    coord_int_t y = ctx->row_offset;

    const utf8_cstr_t *column_text = columns;
    const struct table_column_t *column_decl = ctx->columns;
    for (unsigned int i = 0; i < ctx->column_count; i++) {
        font_draw_text_ellipsis(
            font,
            x, y,
            text_colour,
            *column_text,
            column_decl->width);

        x += column_decl->width;

        column_text++;
        column_decl++;
    }

    ctx->row_offset = y + ctx->row_height;
}
