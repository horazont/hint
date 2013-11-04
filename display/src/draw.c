#include "draw.h"

#include "lcd.h"

inline void clamp_x(coord_int_t *x)
{
    if (*x < 0) {
        *x = 0;
    } else if (*x >= LCD_WIDTH) {
        *x = LCD_WIDTH-1;
    }
}

inline void clamp_y(coord_int_t *y)
{
    if (*y < 0) {
        *y = 0;
    } else if (*y >= LCD_HEIGHT) {
        *y = LCD_HEIGHT-1;
    }
}

inline void rectangle_clamp(coord_int_t *x0, coord_int_t *y0,
                            coord_int_t *x1, coord_int_t *y1)
{
    clamp_x(x0);
    clamp_x(x1);
    clamp_y(y0);
    clamp_y(y1);
}

inline void rectangle_clamp_and_swap(coord_int_t *x0, coord_int_t *y0,
                                     coord_int_t *x1, coord_int_t *y1)
{
    rectangle_clamp(x0, y0, x1, y1);

    if (*x0 > *x1) {
        *x1 ^= *x0;
        *x0 ^= *x1;
        *x1 ^= *x0;
    }

    if (*y0 > *y1) {
        *y1 ^= *y0;
        *y0 ^= *y1;
        *y1 ^= *y0;
    }
}

void fill_rectangle(
    coord_int_t x0,
    coord_int_t y0,
    coord_int_t x1,
    coord_int_t y1,
    const colour_t fill)
{
    rectangle_clamp_and_swap(&x0, &y0, &x1, &y1);

    lcd_setarea(x0, y0, x1, y1);
    const coord_int_t width = (x1 - x0) + 1, height = (y1 - y0) + 1;
    uint32_t size;
    lcd_drawstart();
    for (size = width*height; size > 0; size--) {
        lcd_draw(fill);
    }
    lcd_drawstop();
}

void draw_bitmap_transparent(
    coord_int_t x0,
    coord_int_t y0,
    coord_int_t width,
    coord_int_t height,
    const colour_t colour,
    const uint8_t *bitmap)
{
    clamp_x(&x0);
    clamp_y(&y0);
    if (x0 + width > LCD_WIDTH) {
        return;
    }
    if (y0 + height > LCD_HEIGHT) {
        return;
    }

    const uint8_t *curr_byte_p = bitmap;
    uint8_t curr_byte = *curr_byte_p;
    uint8_t curr_mask = 0x80;

    // lcd_setarea(x0, y0, x0+width, y0+height);
    for (int16_t y = 0; y < height; y++) {
        for (int16_t x = 0; x < width; x++) {
            if (curr_byte & curr_mask) {
                lcd_setpixel(x0+x, y0+y, colour);
            }

            curr_mask >>= 1;
            if (curr_mask == 0x00) {
                curr_byte_p++;
                curr_byte = *curr_byte_p;
                curr_mask = 0x80;
            }
        }
    }
}

void draw_line(
    coord_int_t x0,
    coord_int_t y0,
    coord_int_t x1,
    coord_int_t y1,
    const colour_t colour)
{
    /* this function is taken from the original firmware (see COPYING)
     */
    coord_int_t dx, dy, dx2, dy2, err, stepx, stepy;

    if( (x0 == x1) || //horizontal line
        (y0 == y1))   //vertical line
    {
        fill_rectangle(x0, y0, x1, y1, colour);
    }
    else
    {
        rectangle_clamp(&x0, &y0, &x1, &y1);
        //calculate direction
        dx = x1-x0;
        dy = y1-y0;
        if (dx < 0) {
            dx = -dx;
            stepx = -1;
        } else {
            stepx = +1;
        }
        if (dy < 0) {
            dy = -dy;
            stepy = -1;
        } else {
            stepy = +1;
        }
        dx2 = dx*2;
        dy2 = dy*2;
        //draw line
        lcd_setpixel(x0, y0, colour);
        if(dx > dy)
        {
            err = dy2 - dx;
            while(x0 != x1)
            {
                if(err >= 0)
                {
                    y0  += stepy;
                    err -= dx2;
                }
                x0  += stepx;
                err += dy2;
                lcd_setpixel(x0, y0, colour);
            }
        }
        else
        {
            err = dx2 - dy;
            while(y0 != y1)
            {
                if(err >= 0)
                {
                    x0  += stepx;
                    err -= dy2;
                }
                y0  += stepy;
                err += dx2;
                lcd_setpixel(x0, y0, colour);
            }
        }
    }
}

void draw_rectangle(
    coord_int_t x0,
    coord_int_t y0,
    coord_int_t x1,
    coord_int_t y1,
    const colour_t colour)
{
    draw_line(x0, y0, x1, y0, colour);
    draw_line(x1, y0, x1, y1, colour);
    draw_line(x1, y1, x0, y1, colour);
    draw_line(x0, y1, x0, y0, colour);
}
