#ifndef _SCREEN_H
#define _SCREEN_H

#include "array.h"
#include "comm.h"
#include "common/types.h"
#include "weather.h"
#include "xmppintf.h"

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

typedef void (*screen_func)(struct screen_t *screen);

struct screen_t {
    screen_func show, hide, free, repaint;

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
    struct comm_t *comm,
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

/* screen: departure times */

struct screen_dept_t {
    enum xmpp_request_status_t status;
    struct array_t rows;
};

void screen_dept_init(struct screen_t *screen);
void screen_dept_set_error(
    struct screen_t *screen,
    enum xmpp_request_status_t status);
void screen_dept_update_data(
    struct screen_t *screen,
    struct array_t *new_data);

/* screen: weather */

typedef uint16_t weather_type_t;

#define WEATHER_CLOUD_MASK                  (0x0003)
#define WEATHER_NO_CLOUD                    (0x0000)
#define WEATHER_LIGHT_CLOUD                 (0x0001)
#define WEATHER_DENSE_CLOUD                 (0x0003)

#define WEATHER_PRECIPITATION_MASK          (0x000C)
#define WEATHER_NO_PRECIPITATION            (0x0000)
#define WEATHER_LIGHT_PRECIPITATION         (0x0004)
#define WEATHER_MEDIUM_PRECIPITATION        (0x0008)
#define WEATHER_HEAVY_PRECIPITATION         (0x000C)

#define WEATHER_FREEZING_MASK               (0x0010)
#define WEATHER_FREEZING                    (0x0010)

#define WEATHER_THUNDERSTORM_MASK           (0x0020)
#define WEATHER_THUNDERSTORM                (0x0020)

struct weather_info_t {
    struct weather_interval_t interval;
    weather_type_t type;
};

#define WEATHER_TIMESLOTS                   (6)
#define WEATHER_HOURS_PER_INTERVAL1         (3)
#define WEATHER_HOURS_PER_INTERVAL2         (6)

struct screen_weather_t {
    struct array_t request_array;
    struct weather_info_t timeslots[WEATHER_TIMESLOTS];
};

void screen_weather_free(struct screen_t *screen);
struct array_t *screen_weather_get_request_array(struct screen_t *screen);
void screen_weather_hide(struct screen_t *screen);
void screen_weather_init(struct screen_t *screen);
void screen_weather_repaint(struct screen_t *screen);
void screen_weather_show(struct screen_t *screen);
void screen_weather_update(struct screen_t *screen);

/* screen: misc */

#endif
