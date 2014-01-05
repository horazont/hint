#ifndef _SCREEN_H
#define _SCREEN_H

#include "array.h"
#include "comm.h"
#include "common/types.h"

#define LCD_WIDTH                           (320)
#define LCD_HEIGHT                          (240)

#define SCREEN_MARGIN_TOP                   (22)
#define SCREEN_MARGIN_LEFT                  (0)
#define SCREEN_MARGIN_RIGHT                 (62)
#define SCREEN_MARGIN_BOTTOM                (0)

#define SCREEN_CLIENT_AREA_TOP              (24)
#define SCREEN_CLIENT_AREA_LEFT             (2)
#define SCREEN_CLIENT_AREA_RIGHT            ((LCD_WIDTH-1)-64)
#define SCREEN_CLIENT_AREA_BOTTOM           ((LCD_HEIGHT-1)-2)

#define SCREEN_HEADER_MARGIN_TOP            (0)
#define SCREEN_HEADER_MARGIN_LEFT           (8)
#define SCREEN_HEADER_MARGIN_RIGHT          (72)
#define SCREEN_HEADER_HEIGHT                (22)

#define CLOCK_POSITION_X                    ((LCD_WIDTH-1)-64)
#define CLOCK_POSITION_Y                    (18)

#define TAB_WIDTH                           (60)
#define TAB_HEIGHT                          (28)
#define TAB_PADDING                         (4)

#define MAX_DEPT_ROWS                       (14)

struct screen_t;
struct broker_t;

typedef void (*screen_func)(struct screen_t *screen);

struct screen_t {
    screen_func show, hide, free, repaint;

    struct broker_t *broker;
    struct comm_t *comm;
    char *title;
    char *tab_caption;

    void *private;
};

/* screen: utilities */

void screen_draw_tab(
    struct comm_t *comm,
    const char *name,
    coord_int_t x0,
    coord_int_t y0,
    bool depressed);

/* screen: shared */

void screen_create(
    struct screen_t *screen,
    struct broker_t *broker,
    const char *title,
    const char *tab_caption);
void screen_draw_background(struct screen_t *screen);
void screen_draw_header(struct screen_t *screen);

static inline void screen_hide(struct screen_t *screen)
{
    if (!screen->hide) {
        return;
    }
    screen->hide(screen);
}

static inline void screen_repaint(struct screen_t *screen)
{
    if (!screen->repaint) {
        return;
    }
    screen->repaint(screen);
}

static inline void screen_show(struct screen_t *screen)
{
    if (!screen->show) {
        return;
    }
    screen->show(screen);
}

void screen_free(struct screen_t *screen);

#endif
