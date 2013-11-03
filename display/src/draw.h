#ifndef _DRAW_H
#define _DRAW_H

#include <stdint.h>

#include "lcd.h"

typedef int16_t coord_int_t;

void fill_rectangle(
    coord_int_t x0,
    coord_int_t y0,
    coord_int_t x1,
    coord_int_t y1,
    const colour_t fill);

void draw_bitmap_transparent(
    coord_int_t x0,
    coord_int_t y0,
    coord_int_t width,
    coord_int_t height,
    const colour_t colour,
    const uint8_t *bitmap);
void draw_line(
    coord_int_t x0,
    coord_int_t y0,
    coord_int_t x1,
    coord_int_t y1,
    const colour_t colour);
void draw_rectangle(
    coord_int_t x0,
    coord_int_t y0,
    coord_int_t x1,
    coord_int_t y1,
    const colour_t fill);

#endif
