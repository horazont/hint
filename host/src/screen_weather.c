#include "screen_weather.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "common/comm_lpc1114.h"

#include "lpcdisplay.h"

/* utilities */

static const coord_int_t interval_width_with_space = 125+2;
static const coord_int_t interval_height_with_space = 56+2;

static void align_time(struct tm *time)
{
    if (time->tm_min + time->tm_sec > 0) {
        time->tm_hour += 1;
    }
    time->tm_min = 0;
    time->tm_sec = 0;
}

static inline void calculate_weather_type(struct weather_info_t *info)
{
    weather_type_t type = 0;

    /* precipitation */
    float normalized_precipitation =
        info->interval.precipitation_millimeter / ((info->interval.end - info->interval.start) / 3600);
    if (normalized_precipitation < 0.125) {
        type |= WEATHER_NO_PRECIPITATION;
    } else if (normalized_precipitation < 0.5) {
        type |= WEATHER_LIGHT_PRECIPITATION;
    } else if (normalized_precipitation < 1.5) {
        type |= WEATHER_MEDIUM_PRECIPITATION;
    } else {
        type |= WEATHER_HEAVY_PRECIPITATION;
    }

    /* cloud */
    if (info->interval.cloudiness_percent < 0.25) {
        type |= WEATHER_NO_CLOUD;
    } else if (info->interval.cloudiness_percent < 0.75) {
        type |= WEATHER_LIGHT_CLOUD;
    } else {
        type |= WEATHER_DENSE_CLOUD;
    }

    /* freezing */
    if (info->interval.temperature_celsius < 3) {
        type |= WEATHER_FREEZING;
    }

    /* thunderstrom? we have no info on that! */

    info->type = type;
}

static const char *get_weather_name(weather_type_t type)
{
    switch (type & WEATHER_PRECIPITATION_MASK)
    {
    case WEATHER_NO_PRECIPITATION:
    case WEATHER_LIGHT_PRECIPITATION:
    {
        switch (type & WEATHER_CLOUD_MASK)
        {
        case WEATHER_NO_CLOUD:
        {
            return "Sonne";
        }
        case WEATHER_LIGHT_CLOUD:
        {
            return "Wolken";
        }
        case WEATHER_DENSE_CLOUD:
        {
            return "Dichte Wolken";
        }
        }
    }
    case WEATHER_MEDIUM_PRECIPITATION:
    {
        return "Regen";
    }
    case WEATHER_HEAVY_PRECIPITATION:
    {
        return "Weltuntergang";
    }
    }

    return "???";
}

static const char *get_weather_icon(weather_type_t type)
{
    switch (type & WEATHER_PRECIPITATION_MASK)
    {
    case WEATHER_NO_PRECIPITATION:
    case WEATHER_LIGHT_PRECIPITATION:
    {
        switch (type & WEATHER_CLOUD_MASK)
        {
        case WEATHER_NO_CLOUD:
        {
            return "☀";
        }
        case WEATHER_LIGHT_CLOUD:
        {
            return "⛅";
        }
        case WEATHER_DENSE_CLOUD:
        {
            return "☁";
        }
        }
    }
    case WEATHER_MEDIUM_PRECIPITATION:
    {
        if (type & WEATHER_FREEZING) {
            return "☃";
        }
        return "⛈";
    }
    case WEATHER_HEAVY_PRECIPITATION:
    {
        return "☄";
    }
    }
    return "?";
}

static inline int format_number_with_unit(
    char *buffer,
    size_t buflen,
    const char *valuefmt,
    const char *unit,
    float value)
{
    char *pos = buffer;
    size_t remlen = buflen;
    int total = 0;
    int written = snprintf(
        pos,
        remlen,
        valuefmt,
        value)+1;
    pos += written;
    remlen -= written;
    total += written;

    written = snprintf(
        pos,
        remlen,
        " %s",
        unit)+1;
    pos += written;
    remlen -= written;
    total += written;

    return total;
}

static void draw_weather_interval(
    struct screen_t *screen,
    struct weather_info_t *info,
    coord_int_t x0,
    coord_int_t y0)
{
    char buffer[127];

    static struct table_column_t columns[2];
    columns[0].width = 30;
    columns[0].alignment = TABLE_ALIGN_RIGHT;
    columns[1].width = 30;
    columns[1].alignment = TABLE_ALIGN_LEFT;

    struct tm start, end;
    memcpy(&start, localtime(&info->interval.start), sizeof(struct tm));
    memcpy(&end, localtime(&info->interval.end), sizeof(struct tm));

    lpcd_fill_rectangle(
        screen->comm,
        x0, y0,
        x0+interval_width_with_space-3,
        y0+interval_height_with_space-3,
        0xffff);

    y0 += 12;

    snprintf(
        buffer, sizeof(buffer),
        "%02d:00 – %02d:00",
        start.tm_hour,
        end.tm_hour);
    lpcd_draw_text(
        screen->comm,
        x0, y0,
        LPC_FONT_DEJAVU_SANS_12PX_BF,
        0x0000,
        buffer);

    lpcd_draw_text(
        screen->comm,
        x0, y0+35,
        LPC_FONT_DEJAVU_SANS_40PX,
        0x0000,
        get_weather_icon(info->type));

    y0 += 14;

    lpcd_table_start(
        screen->comm,
        x0+40, y0,
        14, columns, 2);

    intptr_t total_length;
    total_length = format_number_with_unit(
        buffer, sizeof(buffer),
        "%.1f", "°C",
        info->interval.temperature_celsius);
    lpcd_table_row(
        screen->comm,
        LPC_FONT_DEJAVU_SANS_12PX,
        0x0000, 0xffff,
        buffer, total_length);

    total_length = format_number_with_unit(
        buffer, sizeof(buffer),
        "%.1f", "mm",
        info->interval.precipitation_millimeter);
    lpcd_table_row(
        screen->comm,
        LPC_FONT_DEJAVU_SANS_12PX,
        0x0000, 0xffff,
        buffer, total_length);

    total_length = format_number_with_unit(
        buffer, sizeof(buffer),
        "%.1f", "m/s",
        info->interval.windspeed_meter_per_second);
    lpcd_table_row(
        screen->comm,
        LPC_FONT_DEJAVU_SANS_12PX,
        0x0000, 0xffff,
        buffer, total_length);

    lpcd_table_end(screen->comm);

}

static void setup_interval(
    struct weather_interval_t *interval,
    time_t *start_time,
    int hours_per_interval)
{
    const time_t end_time = *start_time + 3600*hours_per_interval;
    struct tm interval_start, interval_end;
    memcpy(&interval_start, gmtime(start_time), sizeof(struct tm));
    memcpy(&interval_end, gmtime(&end_time), sizeof(struct tm));

    align_time(&interval_start);
    align_time(&interval_end);

    interval->start = timegm(&interval_start);
    interval->end = timegm(&interval_end);

    *start_time += 3600*hours_per_interval;
}

/* implementation */

void screen_weather_free(struct screen_t *screen)
{
    struct screen_weather_t *weather = screen->private;
    array_free(&weather->request_array);
    free(weather);
}

struct array_t *screen_weather_get_request_array(struct screen_t *screen)
{
    struct array_t *data =
        &((struct screen_weather_t*)screen->private)->request_array;

    time_t start_time = time(NULL);
    for (int i = 0; i < 2; i++) {
        struct weather_interval_t *interval =
            array_get(data, i);
        setup_interval(interval, &start_time, WEATHER_HOURS_PER_INTERVAL1);
    }
    for (int i = 2; i < WEATHER_TIMESLOTS; i++) {
        struct weather_interval_t *interval =
            array_get(data, i);
        setup_interval(interval, &start_time, WEATHER_HOURS_PER_INTERVAL2);
    }

    return data;
}

void screen_weather_hide(struct screen_t *screen)
{

}

void screen_weather_init(struct screen_t *screen)
{
    screen->show = &screen_weather_show;
    screen->hide = &screen_weather_hide;
    screen->repaint = &screen_weather_repaint;
    screen->free = &screen_weather_free;

    struct screen_weather_t *weather =
        malloc(sizeof(struct screen_weather_t));
    array_init(&weather->request_array, WEATHER_TIMESLOTS);
    for (int i = 0; i < WEATHER_TIMESLOTS; i++) {
        array_append(
            &weather->request_array,
            &weather->timeslots[i].interval);

        struct weather_interval_t *const interval =
            &weather->timeslots[i].interval;
        interval->temperature_celsius = NAN;
        interval->humidity_percent = NAN;
        interval->windspeed_meter_per_second = NAN;
        interval->cloudiness_percent = NAN;
        interval->precipitation_millimeter = NAN;
        weather->timeslots[i].type = 0;
    }

    screen->private = weather;
}

void screen_weather_show(struct screen_t *screen)
{

}

void screen_weather_repaint(struct screen_t *screen)
{
    struct screen_weather_t *weather = screen->private;

    static const coord_int_t x0 = SCREEN_CLIENT_AREA_LEFT;
    static const coord_int_t y0 = SCREEN_CLIENT_AREA_TOP;

    draw_weather_interval(
        screen,
        &weather->timeslots[0],
        x0,
        y0);

    draw_weather_interval(
        screen,
        &weather->timeslots[1],
        x0+interval_width_with_space,
        y0);

    draw_weather_interval(
        screen,
        &weather->timeslots[2],
        x0,
        y0+interval_height_with_space);

    draw_weather_interval(
        screen,
        &weather->timeslots[3],
        x0+interval_width_with_space,
        y0+interval_height_with_space);

    draw_weather_interval(
        screen,
        &weather->timeslots[4],
        x0,
        y0+interval_height_with_space*2);

    draw_weather_interval(
        screen,
        &weather->timeslots[5],
        x0+interval_width_with_space,
        y0+interval_height_with_space*2);

}

void screen_weather_update(struct screen_t *screen)
{
    struct screen_weather_t *weather = screen->private;

    for (int i = 0; i < WEATHER_TIMESLOTS; i++)
    {
        struct weather_info_t *dst = &weather->timeslots[i];
        calculate_weather_type(dst);
    }
}
