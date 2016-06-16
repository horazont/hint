#include "screen_weather.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "common/comm_lpc1114.h"

#include "lpcdisplay.h"
#include "theme.h"
#include "screen_utils.h"

/* utilities */

/* static const coord_int_t interval_width_with_space = 125+2; */
/* static const coord_int_t interval_height_with_space = 56+2; */

#define SCALEBAR_HEIGHT (2)

static inline float clamp(const float v, const float minv, const float maxv)
{
    return (v > maxv ? maxv : v < minv ? minv : v);
}

#ifdef MISSING_FMAXF_FMINF

inline float fmaxf(const float a, const float b)
{
    return (a > b ? a : b);
}

inline float fminf(const float a, const float b)
{
    return (a < b ? a : b);
}

#endif

static inline float mywrapf(float a, float b)
{
    float result = fmodf(a, b);
    if (result < 0) {
        result = b + result;
    }
    return result;
}

static colour_t cubehelix(
    const float gray,
    const float s,
    const float r,
    const float h)
{
    const float a = h*gray*(1.f-gray)/2.f;
    const float phi = 2.*M_PI*(s/3. + r*gray);

    const float cos_phi = cosf(phi);
    const float sin_phi = sinf(phi);

    const float rf = clamp(
        gray + a*(-0.14861*cos_phi + 1.78277*sin_phi),
        0.0, 1.0);
    const float gf = clamp(
        gray + a*(-0.29227*cos_phi - 0.90649*sin_phi),
        0.0, 1.0);
    const float bf = clamp(
        gray + a*(1.97294*cos_phi),
        0.0, 1.0);

    return ((colour_t)(rf * 0x1f) << 11) | ((colour_t)(gf * 0x3f) << 5) | ((colour_t)(bf * 0x1f));
}

static colour_t rgbf_to_rgb16(const float rf, const float gf, const float bf)
{
    const colour_t r = ((colour_t)(rf*31)) & (0x1f);
    const colour_t g = ((colour_t)(gf*63)) & (0x3f);
    const colour_t b = ((colour_t)(bf*31)) & (0x1f);

    return (r << 11) | (g << 5) | b;
}

static colour_t hsv_to_rgb(
    float h,
    const float s,
    const float v)
{
    if (s == 0) {
        const colour_t r_b = (colour_t)(v*31);
        const colour_t g = (colour_t)(v*63);
        return (r_b << 11) | (g << 5) | r_b;
    }

    h = mywrapf(h, M_PI*2.);
    float indexf;
    const float fractional = modff(h / (M_PI*2.f/6.f), &indexf);

    const int index = (int)indexf;

    const float p = v * (1.0f - s);
    const float q = v * (1.0f - (s * fractional));
    const float t = v * (1.0f - (s * (1.0f - fractional)));

    switch (index) {
    case 0:
    {
        return rgbf_to_rgb16(v, t, p);
    }
    case 1:
    {
        return rgbf_to_rgb16(q, v, p);
    }
    case 2:
    {
        return rgbf_to_rgb16(p, v, t);
    }
    case 3:
    {
        return rgbf_to_rgb16(p, q, v);
    }
    case 4:
    {
        return rgbf_to_rgb16(t, p, v);
    }
    case 5:
    {
        return rgbf_to_rgb16(v, p, q);
    }
    }

    return 0x0000;
}

static colour_t cloudcolour(
    float cloudiness,
    float precipitation)
{
    precipitation /= 5.0f;
    cloudiness = clamp(cloudiness/1.5f, 0.0f, 0.6667f);
    precipitation = fmaxf(precipitation, 0.0);

    return hsv_to_rgb(
        (fminf(fmaxf(precipitation - 1.0, 0.0) / 3.0f,
               1./3.f) + 2.f/3.f) * M_PI * 2.f,
        fminf(precipitation, 1.0),
        1.0f - cloudiness);
}

static colour_t tempcolour(
    const float minT,
    const float maxT,
    const float T)
{
    const float normT = clamp((T - minT) / (maxT - minT), 0.0, 1.0);
    return cubehelix(
        normT,
        M_PI/12.,
        -1.0,
        2.0);
}

/* calculate luminance in fixed-point 0.8 format */
static uint8_t luminance(const colour_t colour)
{
    static const uint32_t rfactor = 0x1322d0e;
    static const uint32_t gfactor = 0x2591686;
    static const uint32_t bfactor = 0x74bc6a;

    uint32_t r = ((colour & 0xf800) >> 10) | 1;
    uint32_t g = ((colour & 0x07e0) >> 5);
    uint32_t b = ((colour & 0x001f) << 1) | 1;

    return ((r*rfactor + g*gfactor + b*bfactor) & 0xff000000) >> 24;
}

static void align_time(struct tm *time)
{
    if (time->tm_min + time->tm_sec > 0) {
        time->tm_hour += 1;
    }
    time->tm_min = 0;
    time->tm_sec = 0;
}

static inline int format_number_with_unit(
    char *buffer,
    size_t buflen,
    const char *unit,
    float value)
{
    char *pos = buffer;
    size_t remlen = buflen;
    int total = 0;
    int written = 0;

    if (value < 0) {
        written = snprintf(
            pos,
            remlen,
            "%s",
            "–");
        pos += written;
        remlen += written;
        total += written;
    }

    written = snprintf(
        pos,
        remlen,
        (fabs(value) >= 9.5 ? "%.0f" : "%.1f"),
        fabs(value))+1;

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

static inline void format_dynamic_number(
    struct table_row_formatter_t *dest,
    float value,
    const colour_t fgcolour,
    const colour_t bgcolour,
    const table_column_alignment_t alignment
    )
{
    const char *fmt = NULL;
    if (value < 0) {
        fmt = (fabs(value) > 9.5 ? "–%.0f" : "–%.1f");
    } else {
        fmt = (fabs(value) > 9.5 ? "%.0f" : "%.1f");
    }

    table_row_formatter_append_ex(
        dest,
        fgcolour,
        bgcolour,
        alignment,
        fmt,
        fabs(value)
        );
}


static void draw_weather_bar(
    struct screen_t *screen,
    coord_int_t x0,
    coord_int_t y0,
    struct tm *prev_time,
    struct weather_info_t **interval)
{
    const coord_int_t interval_width = 42;
    const coord_int_t bar_height = 42;
    const coord_int_t text_height = 11;
    const coord_int_t block_height = (bar_height - text_height) / 2;

    const float temp_min = -10.0;
    const float temp_max = 40.0;

    struct weather_info_t *curr_interval = *interval;

    char textbuffer[128];

    struct table_row_formatter_t time_row;
    struct table_row_formatter_t temp_row;
    struct table_row_formatter_t cloud_row;
    // 40 >= 6*(len("XX:XX")+1)
    table_row_formatter_init_dynamic(&time_row, 40);

    // 120 >= 12*(2+2+3+1)  (2 times colour, three characters avg., 1 null byte)
    table_row_formatter_init_dynamic(&temp_row, 120);
    table_row_formatter_init_dynamic(&cloud_row, 120);

    static struct table_column_t time_columns[6] = {
        {
            .width = 42,
            .alignment = TABLE_ALIGN_LEFT,
        },
        {
            .width = 42,
            .alignment = TABLE_ALIGN_LEFT,
        },
        {
            .width = 42,
            .alignment = TABLE_ALIGN_LEFT,
        },
        {
            .width = 42,
            .alignment = TABLE_ALIGN_LEFT,
        },
        {
            .width = 42,
            .alignment = TABLE_ALIGN_LEFT,
        },
        {
            .width = 42,
            .alignment = TABLE_ALIGN_LEFT,
        },
    };

    static struct table_column_t weather_columns[12] = {
        {
            .width = 21,
            .alignment = TABLE_ALIGN_RIGHT,
        },
        {
            .width = 21,
            .alignment = TABLE_ALIGN_LEFT,
        },
        {
            .width = 21,
            .alignment = TABLE_ALIGN_RIGHT,
        },
        {
            .width = 21,
            .alignment = TABLE_ALIGN_LEFT,
        },
        {
            .width = 21,
            .alignment = TABLE_ALIGN_RIGHT,
        },
        {
            .width = 21,
            .alignment = TABLE_ALIGN_LEFT,
        },
        {
            .width = 21,
            .alignment = TABLE_ALIGN_RIGHT,
        },
        {
            .width = 21,
            .alignment = TABLE_ALIGN_LEFT,
        },
        {
            .width = 21,
            .alignment = TABLE_ALIGN_RIGHT,
        },
        {
            .width = 21,
            .alignment = TABLE_ALIGN_LEFT,
        },
        {
            .width = 21,
            .alignment = TABLE_ALIGN_RIGHT,
        },
        {
            .width = 21,
            .alignment = TABLE_ALIGN_LEFT,
        },
    };

    for (size_t hour_offset = 0; hour_offset < 6; hour_offset++)
    {
        struct tm this_time;
        memcpy(&this_time,
               localtime(&curr_interval->interval.start),
               sizeof(struct tm));

        const char *textfmt = NULL;
        if (prev_time->tm_mday != this_time.tm_mday) {
            textfmt = "%d %b";
        } else {
            textfmt = "%H:00";
        }

        strftime(textbuffer,
                 sizeof(textbuffer),
                 textfmt,
                 &this_time);

        table_row_formatter_append(
            &time_row,
            "%s", textbuffer
            );

        colour_t colour = tempcolour(
            temp_min,
            temp_max,
            curr_interval->interval.temperature_celsius);
        colour_t text_colour = 0x0000;

        if (luminance(colour) <= 127) {
            text_colour = 0xffff;
        }

        format_dynamic_number(
            &temp_row,
            curr_interval->interval.temperature_celsius,
            text_colour,
            colour,
            TABLE_ALIGN_RIGHT
            );

        table_row_formatter_append_ex(
            &temp_row,
            text_colour,
            colour,
            TABLE_ALIGN_LEFT,
            "%s",
            "°C"
            );

        colour = cloudcolour(
            curr_interval->interval.cloudiness_percent / 100.,
            0);
        text_colour = 0x0000;

        if (luminance(colour) <= 127) {
            text_colour = 0xffff;
        }

        table_row_formatter_append_ex(
            &cloud_row,
            text_colour,
            colour,
            TABLE_ALIGN_CENTER,
            "%.0f",
            curr_interval->interval.precipitation_probability*100.f
            );

        colour = cloudcolour(
            0,
            curr_interval->interval.precipitation_millimeter);
        text_colour = 0x0000;

        if (luminance(colour) <= 127) {
            text_colour = 0xffff;
        }

        format_dynamic_number(
            &cloud_row,
            curr_interval->interval.precipitation_millimeter,
            text_colour,
            colour,
            TABLE_ALIGN_CENTER
            );

        /* table_row_formatter_append_ex( */
        /*     &cloud_row, */
        /*     text_colour, */
        /*     colour, */
        /*     TABLE_ALIGN_LEFT, */
        /*     "%.0f", */
        /*     curr_interval->interval.precipitation_probability*100.f */
        /*     ); */

        ++curr_interval;
        memcpy(prev_time, &this_time, sizeof(struct tm));
    }

    lpcd_table_start(
        screen->comm,
        x0,
        y0+9,
        text_height,
        time_columns,
        6
        );

    size_t columns_len = 0;
    char *columns = table_row_formatter_get(
        &time_row,
        &columns_len
        );

    lpcd_table_row(
        screen->comm,
        LPC_FONT_DEJAVU_SANS_9PX,
        THEME_CLIENT_AREA_COLOUR,
        THEME_CLIENT_AREA_BACKGROUND_COLOUR,
        columns,
        columns_len
        );

    y0 += text_height;

    lpcd_table_start(
        screen->comm,
        x0,
        y0+9,
        block_height,
        weather_columns,
        12
        );

    columns = table_row_formatter_get(
        &temp_row,
        &columns_len
        );

    lpcd_table_row_ex(
        screen->comm,
        LPC_FONT_DEJAVU_SANS_9PX,
        (const struct table_column_ex_t*)columns,
        columns_len
        );

    columns = table_row_formatter_get(
        &cloud_row,
        &columns_len
        );

    lpcd_table_row_ex(
        screen->comm,
        LPC_FONT_DEJAVU_SANS_9PX,
        (const struct table_column_ex_t*)columns,
        columns_len
        );

    table_row_formatter_free(&time_row);
    table_row_formatter_free(&temp_row);
    table_row_formatter_free(&cloud_row);
    *interval = curr_interval;

    (void)block_height;
    (void)weather_columns;
    (void)interval_width;
}

static void setup_interval(
    struct weather_interval_t *interval,
    time_t *start_time)
{
    const time_t end_time = *start_time + 3600;
    struct tm interval_start, interval_end;
    memcpy(&interval_start, gmtime(start_time), sizeof(struct tm));
    memcpy(&interval_end, gmtime(&end_time), sizeof(struct tm));

    align_time(&interval_start);
    align_time(&interval_end);

    interval->start = timegm(&interval_start);
    interval->end = timegm(&interval_end);

    *start_time += 3600;
}

/* implementation */

void screen_weather_free(struct screen_t *screen)
{
    struct screen_weather_t *weather = screen->private;
    array_free(&weather->request_array);
    free(weather->scalebar);
    free(weather);
}

struct array_t *screen_weather_get_request_array(struct screen_t *screen)
{
    struct array_t *data =
        &((struct screen_weather_t*)screen->private)->request_array;

    time_t start_time = time(NULL);
    for (int i = 0; i < WEATHER_INTERVALS; i++) {
        struct weather_interval_t *interval =
            array_get(data, i);
        setup_interval(interval, &start_time);
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
    array_init(&weather->request_array, WEATHER_INTERVALS);
    for (int i = 0; i < WEATHER_INTERVALS; i++) {
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

    for (int i = 0; i < SENSOR_COUNT; i++) {
        weather->sensors[i].temperature = NAN;
    }

    weather->scalebar = malloc(sizeof(uint16_t)*SCALEBAR_HEIGHT*SCREEN_CLIENT_AREA_WIDTH);

    uint16_t *dest = weather->scalebar;
    for (coord_int_t x = 0; x < SCREEN_CLIENT_AREA_WIDTH; ++x) {
        for (coord_int_t y = 0; y < SCALEBAR_HEIGHT; ++y) {
            float prec = ((float)x)/(SCREEN_CLIENT_AREA_WIDTH-1);
            *dest++ = cloudcolour(0, prec);
        }
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
    static const coord_int_t y0 = SCREEN_CLIENT_AREA_TOP+22;

    coord_int_t current_y0 = y0 - 6;
    char buffer[127];

    lpcd_fill_rectangle(
        screen->comm,
        x0,
        SCREEN_CLIENT_AREA_TOP,
        SCREEN_CLIENT_AREA_RIGHT-1,
        current_y0+6,
        THEME_CLIENT_AREA_BACKGROUND_COLOUR);

    snprintf(
        buffer,
        sizeof(buffer),
        "Außenwelt: %5.1f °C",
        weather->sensors[SENSOR_EXTERIOR].temperature);
    lpcd_draw_text(
        screen->comm,
        x0,
        current_y0,
        LPC_FONT_DEJAVU_SANS_12PX,
        THEME_CLIENT_AREA_COLOUR,
        buffer);

    snprintf(
        buffer,
        sizeof(buffer),
        "Innen: %5.1f °C",
        weather->sensors[SENSOR_INTERIOR].temperature);
    lpcd_draw_text(
        screen->comm,
        x0+(SCREEN_CLIENT_AREA_RIGHT - SCREEN_CLIENT_AREA_LEFT - 1)/2,
        current_y0,
        LPC_FONT_DEJAVU_SANS_12PX,
        THEME_CLIENT_AREA_COLOUR,
        buffer);

    const time_t now = time(NULL);
    struct tm prev_time;
    memcpy(&prev_time, localtime(&now), sizeof(struct tm));

    struct weather_info_t *curr_interval = &weather->timeslots[0];

    draw_weather_bar(
        screen,
        x0, y0,
        &prev_time,
        &curr_interval);

    draw_weather_bar(
        screen,
        x0, y0+48,
        &prev_time,
        &curr_interval);

    draw_weather_bar(
        screen,
        x0, y0+48*2,
        &prev_time,
        &curr_interval);

    draw_weather_bar(
        screen,
        x0, y0+48*3,
        &prev_time,
        &curr_interval);

    /* lpcd_image_start( */
    /*     screen->comm, */
    /*     SCREEN_CLIENT_AREA_LEFT, */
    /*     SCREEN_CLIENT_AREA_BOTTOM-2, */
    /*     SCREEN_CLIENT_AREA_RIGHT, */
    /*     SCREEN_CLIENT_AREA_BOTTOM-1 */
    /*     ); */

    /* { */
    /*     const uint16_t buffer_len = 100; */
    /*     const uint16_t buffer_size = buffer_len * sizeof(uint16_t); */
    /*     const uint16_t cols_per_message = buffer_size / SCALEBAR_HEIGHT; */
    /*     uint16_t *buffer = malloc(buffer_size); */
    /*     uint16_t *const buffer_end = &buffer[buffer_len]; */
    /*     uint16_t *dest = buffer; */

    /*     for (int prec = 0; prec < SCREEN_CLIENT_AREA_WIDTH; ++prec) { */
    /*         float precf = ((float)prec) / SCREEN_CLIENT_AREA_WIDTH; */
    /*         uint16_t colour = cloudcolour(0.f, precf); */

    /*         for (int row = 0; row < cols_per_message; ++row) { */
    /*             *dest++ = colour; */
    /*         } */

    /*         if (dest == buffer_end) { */
    /*             lpcd_image_data(screen->comm, buffer, buffer_size); */
    /*             dest = buffer; */
    /*         } */
    /*     } */

    /*     if (dest != buffer) { */
    /*         lpcd_image_data(screen->comm, buffer, (size_t)(dest - buffer)); */
    /*     } */

    /*     lpcd_image_end(screen->comm); */
    /*     free(buffer); */
    /* } */

    /* draw_weather_interval( */
    /*     screen, */
    /*     &weather->timeslots[0], */
    /*     x0, */
    /*     y0); */

    /* draw_weather_interval( */
    /*     screen, */
    /*     &weather->timeslots[1], */
    /*     x0+interval_width_with_space, */
    /*     y0); */

    /* draw_weather_interval( */
    /*     screen, */
    /*     &weather->timeslots[2], */
    /*     x0, */
    /*     y0+interval_height_with_space); */

    /* draw_weather_interval( */
    /*     screen, */
    /*     &weather->timeslots[3], */
    /*     x0+interval_width_with_space, */
    /*     y0+interval_height_with_space); */

    /* draw_weather_interval( */
    /*     screen, */
    /*     &weather->timeslots[4], */
    /*     x0, */
    /*     y0+interval_height_with_space*2); */

    /* draw_weather_interval( */
    /*     screen, */
    /*     &weather->timeslots[5], */
    /*     x0+interval_width_with_space, */
    /*     y0+interval_height_with_space*2); */

}

void screen_weather_update(struct screen_t *screen)
{

}

void screen_weather_set_sensor(
    struct screen_t *screen,
    int sensor_id,
    int16_t raw_value)
{
    if (sensor_id < 0 || sensor_id >= SENSOR_COUNT) {
        return;
    }

    struct screen_weather_t *weather = screen->private;
    struct screen_weather_sensor_t *sensor = &weather->sensors[sensor_id];
    sensor->last_update = time(NULL);
    sensor->temperature = raw_value / 16.;
}


void screen_weathergraph_init_shared(
    struct screen_t *weathergraph,
    struct screen_t *existing_screen)
{
    memset(existing_screen, 0, sizeof(struct screen_t));

    existing_screen->private = existing_screen->private;
}
