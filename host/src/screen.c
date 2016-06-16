#include "screen.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

#include "common/types.h"
#include "common/comm_lpc1114.h"

#include "theme.h"
#include "lpcdisplay.h"
#include "utils.h"
#include "broker.h"

/* screen: utilities */

void screen_draw_tab(
    struct comm_t *comm,
    const char *name,
    coord_int_t x0,
    coord_int_t y0,
    bool depressed)
{
    colour_t bgcolour = (depressed
                         ? THEME_TAB_ACTIVE_BACKGROUND_COLOUR
                         : THEME_TAB_BACKGROUND_COLOUR);
    colour_t textcolour = (depressed
                           ? THEME_TAB_ACTIVE_COLOUR
                           : THEME_TAB_COLOUR);
    colour_t linecolour = (depressed
                           ? THEME_TAB_ACTIVE_BORDER_COLOUR
                           : THEME_TAB_BORDER_COLOUR);

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
    struct broker_t *broker,
    const char *title,
    const char *tab_caption)
{
    screen->show = NULL;
    screen->hide = NULL;
    screen->free = NULL;
    screen->repaint = NULL;
    screen->touch = NULL;
    screen->broker = broker;
    screen->comm = broker->comm;
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
        THEME_CLIENT_AREA_BACKGROUND_COLOUR);
    lpcd_draw_rectangle(
        screen->comm,
        SCREEN_MARGIN_LEFT+1, SCREEN_MARGIN_TOP+1,
        (LCD_WIDTH-1)-SCREEN_MARGIN_RIGHT-1, (LCD_HEIGHT-1)-SCREEN_MARGIN_BOTTOM-1,
        THEME_CLIENT_AREA_BORDER_COLOUR);
}

void screen_draw_header(struct screen_t *screen)
{
    lpcd_fill_rectangle(
        screen->comm,
        SCREEN_HEADER_MARGIN_LEFT, SCREEN_HEADER_MARGIN_TOP,
        (LCD_WIDTH-1)-SCREEN_HEADER_MARGIN_RIGHT, SCREEN_HEADER_HEIGHT,
        THEME_H1_BACKGROUND_COLOUR);

    lpcd_draw_rectangle(
        screen->comm,
        SCREEN_HEADER_MARGIN_LEFT+1, SCREEN_HEADER_MARGIN_TOP+1,
        (LCD_WIDTH-1)-SCREEN_HEADER_MARGIN_RIGHT-1, SCREEN_HEADER_HEIGHT,
        THEME_H1_BORDER_COLOUR);

    lpcd_draw_text(
        screen->comm,
        SCREEN_HEADER_MARGIN_LEFT+4, SCREEN_HEADER_MARGIN_TOP+16,
        LPC_FONT_DEJAVU_SANS_12PX_BF,
        THEME_H1_COLOUR,
        screen->title);
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
