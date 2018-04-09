#include "screen_net.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <net/if.h>

#include "common/comm_lpc1114.h"

#include "broker.h"
#include "lpcdisplay.h"
#include "screen_utils.h"

#define UPDATE_INTERVAL (3000)
#define PROC_PARSE_BUFFER (512)

const char *ifs[SCREEN_NET_IF_COUNT] = {
    /* "dsl", */
    /* "eth0", */
    /* "ath0" */
    "eth0",
    "p4p1",
    "lo"
};

static const char *parser_format_string_template =
    "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu";
static char parser_format_string[64];


/* utilities */

static int index_of_dev(const char *name)
{
    for (int i = 0; i < SCREEN_NET_IF_COUNT; i++) {
        if (strcmp(name, ifs[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static size_t get_name(
    const char *buf,
    char *name,
    size_t maxlen,
    size_t *col)
{
    size_t skipped = 0;
    while (isspace(*buf)) {
        buf++;
        skipped++;
    }

    size_t len = 1;
    while (*buf != ':')
    {
        len++;
        if (len < maxlen) {
            *name++ = *buf;
        }
        buf += 1;
    }
    if (len < maxlen) {
        *name = '\0';
    }

    if (col) {
        *col = len+skipped;
    }

    return len;
}

static void shift_stats(
    uint64_t stat_array[SCREEN_NET_IF_BACKLOG])
{
    memmove(
        &stat_array[0],
        &stat_array[1],
        sizeof(uint64_t) * (SCREEN_NET_IF_BACKLOG-1));
}

static void parse_stats(
    const char *buf,
    struct net_dev_t *dest,
    uint64_t delta)
{
    uint64_t rx_bytes, tx_bytes, dummy;
    sscanf(buf,
           parser_format_string,
           &rx_bytes,
           &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy,
           &tx_bytes,
           &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy);

    shift_stats(dest->tx_kbytes_per_second);
    shift_stats(dest->rx_kbytes_per_second);

    dest->tx_kbytes_per_second[SCREEN_NET_IF_BACKLOG-1] =
        1000 * (tx_bytes - dest->tx_bytes_prev) / delta / 1024;
    dest->rx_kbytes_per_second[SCREEN_NET_IF_BACKLOG-1] =
        1000 * (rx_bytes - dest->rx_bytes_prev) / delta / 1024;

    dest->tx_bytes_prev = tx_bytes;
    dest->rx_bytes_prev = rx_bytes;
}

static bool regular_update(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata)
{
    timestamp_gettime_in_future(
        next_run, UPDATE_INTERVAL);

    struct screen_net_t *const net = ((struct screen_t*)userdata)->private;

    struct timespec now;
    timestamp_gettime(&now);
    uint64_t delta = timestamp_delta_in_msec(&now, &net->last_update);
    memcpy(&net->last_update, &now, sizeof(struct timespec));

    FILE *procnet = fopen("/proc/net/dev", "r");
    if (!procnet) {
        fprintf(stderr, "screen_net: failed to open /proc/net/dev\n");
        return false;
    }

    /* this code is heavily based on the code in interface.c from
     * net-tools */

    char buf[PROC_PARSE_BUFFER];
    fgets(buf, sizeof(buf), procnet);
    fgets(buf, sizeof(buf), procnet);

    while (fgets(buf, sizeof(buf), procnet))
    {
        char namebuf[IFNAMSIZ];
        size_t start;
        get_name(buf, namebuf, IFNAMSIZ, &start);
        int index = index_of_dev(namebuf);
        if (index < 0) {
            continue;
        }

        parse_stats(&buf[start], &net->devs[index], delta);

    }

    fclose(procnet);

    if (broker->active_screen == SCREEN_NET) {
        screen_repaint(userdata);
    }

    return true;
}

/* implementation */

void screen_net_free(struct screen_t *screen)
{
    broker_remove_task_func(screen->broker, &regular_update);
    free(screen->private);
}

void screen_net_hide(struct screen_t *screen)
{

}

void screen_net_init(struct screen_t *screen)
{
    screen->free = &screen_net_free;
    screen->hide = &screen_net_hide;
    screen->show = &screen_net_show;
    screen->repaint = &screen_net_repaint;

    struct screen_net_t *net = malloc(sizeof(struct screen_net_t));
    timestamp_gettime(&net->last_update);

    strcpy(parser_format_string, parser_format_string_template);
    if (sizeof(long unsigned int) != sizeof(uint64_t)) {
        for (char *ch = parser_format_string;
             *ch != '\0';
             ch++)
        {
            if (*ch == 'l') {
                *ch = 'L';
            }
        }
    }

    for (int i = 0; i < SCREEN_NET_IF_COUNT; i++) {
        net->devs[i].name = ifs[i];
        net->devs[i].tx_bytes_prev = 0;
        net->devs[i].rx_bytes_prev = 0;
        memset(
            &net->devs[i].tx_kbytes_per_second[0],
            0,
            sizeof(net->devs[i].tx_kbytes_per_second));
        memset(
            &net->devs[i].rx_kbytes_per_second[0],
            0,
            sizeof(net->devs[i].rx_kbytes_per_second));
    }

    screen->private = net;

    broker_enqueue_new_task_in(
        screen->broker,
        &regular_update,
        UPDATE_INTERVAL,
        screen);
}

void screen_net_repaint(struct screen_t *screen)
{
    screen_draw_background(screen);

    struct screen_net_t *const net = screen->private;

    static struct table_column_t columns[5];
    columns[0].width = 48;
    columns[0].alignment = TABLE_ALIGN_LEFT;
    columns[1].width = 64;
    columns[1].alignment = TABLE_ALIGN_RIGHT;
    columns[2].width = 24;
    columns[2].alignment = TABLE_ALIGN_LEFT;
    columns[3].width = 64;
    columns[3].alignment = TABLE_ALIGN_RIGHT;
    columns[4].width = 24;
    columns[4].alignment = TABLE_ALIGN_LEFT;

    const coord_int_t x0 = SCREEN_CLIENT_AREA_LEFT;
    const coord_int_t y0 = SCREEN_CLIENT_AREA_TOP;

    lpcd_table_start(
        screen->comm,
        x0, y0+14,
        14, columns, 5);

    static const char *header = "iface\0up\0\0down\0";

    lpcd_table_row(
        screen->comm,
        LPC_FONT_DEJAVU_SANS_12PX_BF,
        0x0000, 0xffff,
        header, 16);


    char buffer[PROC_PARSE_BUFFER];
    size_t len;

    struct table_row_formatter_t formatter;
    table_row_formatter_init(
        &formatter,
        buffer,
        PROC_PARSE_BUFFER);

    for (int i = 0; i < SCREEN_NET_IF_COUNT; i++) {
        struct net_dev_t *const dev = &net->devs[i];

        table_row_formatter_reset(&formatter);
        table_row_formatter_append(
            &formatter,
            "%s",
            dev->name);
        table_row_formatter_append(
            &formatter,
            "%ld",
            dev->tx_kbytes_per_second[SCREEN_NET_IF_BACKLOG-1]);
        table_row_formatter_append(
            &formatter,
            " kB");
        table_row_formatter_append(
            &formatter,
            "%ld",
            dev->rx_kbytes_per_second[SCREEN_NET_IF_BACKLOG-1]);
        table_row_formatter_append(
            &formatter,
            " kB");
        table_row_formatter_get(&formatter, &len);
        lpcd_table_row(
            screen->comm,
            LPC_FONT_DEJAVU_SANS_12PX,
            0x0000, 0xffff,
            buffer, len);
    }

    table_row_formatter_free(&formatter);
}

void screen_net_show(struct screen_t *screen)
{

}
