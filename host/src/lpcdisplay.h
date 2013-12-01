#ifndef _LPCDISPLAY_H
#define _LPCDISPLAY_H

#include "common/types.h"

#include "comm.h"

void lpcd_draw_rectangle(
    struct comm_t *comm,
    const coord_int_t x0,
    const coord_int_t y0,
    const coord_int_t x1,
    const coord_int_t y1,
    const colour_t colour);

void lpcd_draw_text(
    struct comm_t *comm,
    const char *text,
    const int font,
    const coord_int_t x0,
    const coord_int_t y0,
    const colour_t colour);

void lpcd_fill_rectangle(
    struct comm_t *comm,
    const coord_int_t x0,
    const coord_int_t y0,
    const coord_int_t x1,
    const coord_int_t y1,
    const colour_t colour);

void lpcd_table_end(
    struct comm_t *comm);

void lpcd_table_row(
    struct comm_t *comm,
    const int font,
    const colour_t fgcolour,
    const colour_t bgcolour,
    const char *columns,
    const int columns_len);

void lpcd_table_start(
    struct comm_t *comm,
    const coord_int_t x0,
    const coord_int_t y0,
    const coord_int_t row_height,
    const struct table_column_t columns[],
    const int column_count);

#endif
