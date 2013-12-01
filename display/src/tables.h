#ifndef _TABLES_H
#define _TABLES_H

#include "draw.h"
#include "font.h"

#include "common/types.h"

struct table_t {
    const struct table_column_t *columns;
    uint8_t column_count;
    coord_int_t row_height;

    // private
    coord_int_t x0, row_offset;
};

/**
 * Initialize a table structure.
 *
 * @param ctx Table context to initialize
 * @param columns Declarations for the columns of the table
 * @param row_height Height of the table rows, this is const and should
 *        be greater than or equal to the font height + depth.
 */
void table_init(
    struct table_t *ctx,
    const struct table_column_t *columns,
    const uint8_t column_count,
    const coord_int_t row_height);

/**
 * Start drawing a table.
 *
 * Reset the table structure to begin drawing at a position.
 *
 * @param ctx Table context to use
 * @param x0 x position to start drawing at
 * @param y0 y position to start drawing at
 */
void table_start(
    struct table_t *ctx,
    const coord_int_t x0,
    const coord_int_t y0);

/**
 * Draw a table row
 *
 * @param ctx Table context to use
 * @param font Font to use
 * @param columns One string for each column. This must be at least as
 *        many strings as columns are defined.
 */
void table_row(
    struct table_t *ctx,
    const struct font_t *font,
    const utf8_cstr_t *columns,
    const colour_t fgcolour,
    const colour_t bgcolour);

/**
 * Draw a table row
 *
 * @param ctx table context to use
 * @param font to use
 * @param columns a long string containing the columns. The text for
 *        each column should be separated by a NUL character (so the
 *        buffer pointed to should be a packed sequence of c strings)
 * @param text_colour foreground colour to use
 */
void table_row_onebuffer(
    struct table_t *ctx,
    const struct font_t *font,
    utf8_cstr_t columns,
    const colour_t fgcolour,
    const colour_t bgcolour);

#endif
