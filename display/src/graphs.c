#include "graphs.h"

void graph_background(
    struct graph_axes_t *ctx,
    const colour_t background_colour)
{
    fill_rectangle(ctx->x0, ctx->y0,
                   ctx->x0+ctx->width-1, ctx->y0+ctx->height-1,
                   background_colour);
}

inline void graph_line_straight(
    struct graph_axes_t *ctx,
    const struct data_point_t data[],
    const unsigned int count,
    const colour_t line_colour)
{
    const coord_int_t x0 = ctx->x0;
    const coord_int_t y0 = ctx->y0 + ctx->height - 1;
    const struct data_point_t *curr_data = data;
    coord_int_t prevx, prevy, currx, curry;

    prevx = x0 + curr_data->x;
    prevy = y0 - curr_data->y;
    for (unsigned int i = 1; i < count; i++) {
        ++curr_data;

        currx = x0 + curr_data->x;
        curry = y0 - curr_data->y;

        draw_line(prevx, prevy, currx, curry, line_colour);

        prevx = currx;
        prevy = curry;
    }
}

inline void graph_line_step(
    struct graph_axes_t *ctx,
    const struct data_point_t data[],
    const unsigned int count,
    const colour_t line_colour)
{
    const coord_int_t x0 = ctx->x0;
    const coord_int_t y0 = ctx->y0 + ctx->height - 1;
    const struct data_point_t *curr_data = data;
    coord_int_t prevx, prevy, currx, curry;

    prevx = x0 + curr_data->x;
    prevy = y0 - curr_data->y;
    for (unsigned int i = 1; i < count; i++) {
        ++curr_data;

        currx = x0 + curr_data->x;
        curry = y0 - curr_data->y;

        draw_line(prevx, prevy, currx, prevy, line_colour);
        draw_line(currx, prevy, currx, curry, line_colour);

        prevx = currx;
        prevy = curry;
    }
}

void graph_x_axis(
    struct graph_axes_t *ctx,
    const colour_t line_colour,
    const coord_int_t yoffs)
{
    const coord_int_t y = ctx->y0 + ctx->height - (1 + yoffs);
    const coord_int_t x = ctx->x0 + ctx->width - (1 + yoffs);
    draw_line(ctx->x0, y, x, y, line_colour);

    // draw arrow
    lcd_setpixel(x+1, y, line_colour);
    lcd_setpixel(x+2, y, line_colour);
    lcd_setpixel(x, y+1, line_colour);
    lcd_setpixel(x, y+2, line_colour);
    lcd_setpixel(x, y-1, line_colour);
    lcd_setpixel(x, y-2, line_colour);
    lcd_setpixel(x+1, y+1, line_colour);
    lcd_setpixel(x+1, y-1, line_colour);
}

void graph_y_axis(
    struct graph_axes_t *ctx,
    const colour_t line_colour,
    const coord_int_t xoffs)
{
    const coord_int_t y = ctx->y0;
    const coord_int_t x = ctx->x0 + xoffs;
    draw_line(ctx->x0, ctx->y0, ctx->x0, ctx->y0+ctx->height-1, line_colour);

    // draw arrow
    lcd_setpixel(x+1, y, line_colour);
    lcd_setpixel(x+2, y, line_colour);
    lcd_setpixel(x-1, y, line_colour);
    lcd_setpixel(x-2, y, line_colour);
    lcd_setpixel(x, y-1, line_colour);
    lcd_setpixel(x, y-2, line_colour);
    lcd_setpixel(x+1, y-1, line_colour);
    lcd_setpixel(x-1, y-1, line_colour);
}
