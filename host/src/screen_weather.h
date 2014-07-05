#ifndef _SCREEN_WEATHER_H
#define _SCREEN_WEATHER_H

#include "screen.h"
#include "weather.h"

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

#define WEATHER_INTERVALS                   (24)

#define SENSOR_EXTERIOR                     (0)
#define SENSOR_INTERIOR                     (1)
#define SENSOR_COUNT                        (2)

struct screen_weather_sensor_t {
    time_t last_update;
    float temperature;
};

struct screen_weather_t {
    struct array_t request_array;
    struct weather_info_t timeslots[WEATHER_INTERVALS];
    struct screen_weather_sensor_t sensors[SENSOR_COUNT];
};

void screen_weather_free(struct screen_t *screen);
struct array_t *screen_weather_get_request_array(struct screen_t *screen);
void screen_weather_hide(struct screen_t *screen);
void screen_weather_init(struct screen_t *screen);
void screen_weather_repaint(struct screen_t *screen);
void screen_weather_show(struct screen_t *screen);
void screen_weather_update(struct screen_t *screen);

void screen_weather_set_sensor(
    struct screen_t *screen,
    int sensor_id,
    int16_t raw_value);

#endif
