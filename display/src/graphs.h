#ifndef _GRAPHS_H
#define _GRAPHS_H

#include "draw.h"

enum line_type_t {
    LINE_STRAIGHT = 0,
    LINE_STEP = 1
};

typedef int16_t data_t;

struct graph_axes_t {
    coord_int_t x0;
    coord_int_t y0;
    coord_int_t width;
    coord_int_t height;
    int16_t ymin, ymax;
};

struct data_point_t {
    data_t x, y;
};

void graph_background(
    struct graph_axes_t *ctx,
    const colour_t background_colour);
void graph_line_straight(
    struct graph_axes_t *ctx,
    const struct data_point_t data[],
    const unsigned int count,
    const colour_t line_colour);
void graph_line_step(
    struct graph_axes_t *ctx,
    const struct data_point_t data[],
    const unsigned int count,
    const colour_t line_colour);
void graph_x_axis(
    struct graph_axes_t *ctx,
    const colour_t line_colour,
    const coord_int_t yoffs);
void graph_y_axis(
    struct graph_axes_t *ctx,
    const colour_t line_colour,
    const coord_int_t xoffs);

inline void graph_line(
    struct graph_axes_t *ctx,
    const struct data_point_t data[],
    const unsigned int count,
    const colour_t line_colour,
    const enum line_type_t line_type)
{
    if (count == 0) {
        return;
    }

    switch (line_type) {
    case LINE_STRAIGHT:
        return graph_line_straight(ctx, data, count, line_colour);
    case LINE_STEP:
        return graph_line_step(ctx, data, count, line_colour);
    }
}

#endif
