#include "screen.h"

#include <stdlib.h>
#include <string.h>

#include "common/types.h"
#include "common/comm_lpc1114.h"

#include "lpcdisplay.h"

/* screen: utilities */

void screen_draw_tab(
    struct comm_t *comm,
    const char *name,
    coord_int_t x0,
    coord_int_t y0,
    bool depressed)
{
    colour_t bgcolour = (depressed ? 0xffff : 0x0000);
    colour_t textcolour = (depressed ? 0x0000 : 0xffff);
    colour_t linecolour = (depressed ? 0x0000 : 0x7bef);

    x0 += (depressed ? -1 : 1);

    lpcd_fill_rectangle(
        comm,
        x0, y0,
        x0+TAB_WIDTH-2, y0+TAB_HEIGHT-1,
        bgcolour);
    lpcd_draw_line(
        comm,
        x0+TAB_WIDTH-1, y0+1,
        x0+TAB_WIDTH-1, y0+TAB_HEIGHT-2,
        bgcolour);

    lpcd_draw_line(
        comm,
        x0, y0+1,
        x0+TAB_WIDTH-3, y0+1,
        linecolour);
    lpcd_draw_line(
        comm,
        x0+TAB_WIDTH-2, y0+2,
        x0+TAB_WIDTH-2, y0+TAB_HEIGHT-3,
        linecolour);
    lpcd_draw_line(
        comm,
        x0+TAB_WIDTH-3, y0+TAB_HEIGHT-2,
        x0, y0+TAB_HEIGHT-2,
        linecolour);

    lpcd_draw_text(
        comm,
        x0+2, y0+6+TAB_HEIGHT/2,
        LPC_FONT_DEJAVU_SANS_12PX,
        textcolour,
        name);
}

/* screen: shared */

void screen_create(
    struct screen_t *screen,
    struct comm_t *comm,
    const char *title,
    const char *tab_caption)
{
    screen->show = NULL;
    screen->hide = NULL;
    screen->free = NULL;
    screen->repaint = NULL;
    screen->comm = comm;
    screen->private = NULL;
    screen->title = strdup(title);
    screen->tab_caption = strdup(tab_caption);
}

void screen_draw_background(struct screen_t *screen)
{
    lpcd_fill_rectangle(
        screen->comm,
        SCREEN_MARGIN_LEFT, SCREEN_MARGIN_TOP,
        (LCD_WIDTH-1)-SCREEN_MARGIN_RIGHT, (LCD_HEIGHT-1)-SCREEN_MARGIN_BOTTOM,
        0xffff);
    lpcd_draw_rectangle(
        screen->comm,
        SCREEN_MARGIN_LEFT+1, SCREEN_MARGIN_TOP+1,
        (LCD_WIDTH-1)-SCREEN_MARGIN_RIGHT-1, (LCD_HEIGHT-1)-SCREEN_MARGIN_BOTTOM-1,
        0x0000);
}

void screen_draw_header(struct screen_t *screen)
{
    lpcd_fill_rectangle(
        screen->comm,
        SCREEN_HEADER_MARGIN_LEFT, SCREEN_HEADER_MARGIN_TOP,
        (LCD_WIDTH-1)-SCREEN_HEADER_MARGIN_RIGHT, SCREEN_HEADER_HEIGHT,
        0xffff);

    lpcd_draw_rectangle(
        screen->comm,
        SCREEN_HEADER_MARGIN_LEFT+1, SCREEN_HEADER_MARGIN_TOP+1,
        (LCD_WIDTH-1)-SCREEN_HEADER_MARGIN_RIGHT-1, SCREEN_HEADER_HEIGHT,
        0x0000);

    lpcd_draw_text(
        screen->comm,
        SCREEN_HEADER_MARGIN_LEFT+4, SCREEN_HEADER_MARGIN_TOP+16,
        LPC_FONT_DEJAVU_SANS_12PX_BF, 0x0000, screen->title);
}

void screen_free(struct screen_t *screen)
{
    if (!screen) {
        return;
    }
    screen->free(screen);
    free(screen->title);
    free(screen->tab_caption);
}

/* screen: departure times */

const char *screen_dept_get_title(struct screen_t *screen);
void screen_dept_free(struct screen_t *screen);
void screen_dept_hide(struct screen_t *screen);
void screen_dept_init(struct screen_t *screen);
void screen_dept_repaint(struct screen_t *screen);
void screen_dept_show(struct screen_t *screen);

void screen_dept_free(struct screen_t *screen)
{
    struct screen_dept_t *dept = screen->private;
    for (int i = 0; i < array_length(&dept->rows); i++) {
        free(array_get(&dept->rows, i));
    }
    free(dept);
}

void screen_dept_hide(struct screen_t *screen)
{

}

void screen_dept_init(struct screen_t *screen)
{
    screen->show = &screen_dept_show;
    screen->hide = &screen_dept_hide;
    screen->free = &screen_dept_free;
    screen->repaint = &screen_dept_repaint;

    struct screen_dept_t *dept = malloc(sizeof(struct screen_dept_t));
    array_init(&dept->rows, 12);

    screen->private = dept;
}

void screen_dept_repaint(struct screen_t *screen)
{
    static struct table_column_t columns[3];
    columns[0].width = 28;
    columns[0].alignment = TABLE_ALIGN_LEFT;
    columns[1].width = 120;
    columns[1].alignment = TABLE_ALIGN_LEFT;
    columns[2].width = 28;
    columns[2].alignment = TABLE_ALIGN_RIGHT;

    static const char* header = "L#\0Fahrtziel\0min";

    lpcd_table_start(
        screen->comm,
        SCREEN_CLIENT_AREA_LEFT, SCREEN_CLIENT_AREA_TOP+14,
        14, columns, 3);

    lpcd_table_row(
        screen->comm,
        LPC_FONT_DEJAVU_SANS_12PX_BF,
        0x0000, 0xffff,
        header, 17);

    lpcd_table_end(screen->comm);
}

void screen_dept_show(struct screen_t *screen)
{
    screen_draw_background(screen);
    screen->repaint(screen);
}

/* screen: weather */

void screen_weather_init(struct screen_t *screen)
{
    screen->private = NULL;
}
