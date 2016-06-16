#include "screen_misc.h"

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <string.h>

#include "common/comm_lpc1114.h"

#include "broker.h"
#include "lpcdisplay.h"
#include "screen_utils.h"

#include "xmppintf.h"

#define UPDATE_INTERVAL (3000)
#define STAT_TABLE_FORMATTER_SIZE (128)
#define STATUS_OK ("\xe2\x9c\x94")
#define STATUS_FAIL ("\xe2\x9c\x98")


struct screen_misc_t {
    struct xmpp_t *xmpp;
};


/* utilities */

struct sysstat_t {
    long uptime;
    float load;
    uint32_t mem_total;
    uint32_t mem_free;
    uint32_t swap_total;
    uint32_t swap_free;
    uint32_t maxrss;
};

static bool regular_update(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata)
{
    timestamp_gettime_in_future(
        next_run, UPDATE_INTERVAL);
    struct screen_t *const screen = userdata;
    screen_repaint(screen);
    return true;
}

static void status_table(
    struct broker_t *const broker,
    coord_int_t x0,
    coord_int_t y0)
{
    struct comm_t *const comm = broker->comm;

    static struct table_column_t columns[2];
    columns[0].width = 64;
    columns[0].alignment = TABLE_ALIGN_LEFT;
    columns[1].width = 25;
    columns[1].alignment = TABLE_ALIGN_LEFT;

    lpcd_table_start(
        comm,
        x0, y0+14,
        14, columns, 2);

    char *buffer;
    size_t len;

    struct table_row_formatter_t formatter;
    table_row_formatter_init_dynamic(
        &formatter,
        STAT_TABLE_FORMATTER_SIZE);

    table_row_formatter_reset(&formatter);
    table_row_formatter_append(
        &formatter,
        "XMPP:");
    if (xmppintf_is_available(broker->xmpp)) {
        table_row_formatter_append(
            &formatter,
            STATUS_OK);
    } else {
        table_row_formatter_append(
            &formatter,
            STATUS_FAIL);
    }
    buffer = table_row_formatter_get(&formatter, &len);
    lpcd_table_row(
        comm,
        LPC_FONT_DEJAVU_SANS_12PX,
        0x0000, 0xffff,
        buffer, len);


    table_row_formatter_free(&formatter);

}

static bool sysstat_read(
    struct sysstat_t *stat)
{
    {
        struct sysinfo info;
        if (sysinfo(&info) != 0) {
            return false;
        }

        stat->uptime = info.uptime;
        stat->mem_total = info.totalram * info.mem_unit / 1024;
        stat->mem_free = info.freeram * info.mem_unit / 1024;
        stat->swap_total = info.totalswap * info.mem_unit / 1024;
        stat->swap_free = info.freeswap * info.mem_unit / 1024;
        stat->load = info.loads[1] / (float)(1 << 16);
    }

    {
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) != 0) {
            return false;
        }

        stat->maxrss = usage.ru_maxrss;
    }

    return true;
}

static void sysstat_table(
    struct comm_t *comm,
    coord_int_t x0,
    coord_int_t y0)
{
    struct sysstat_t sysstat;
    sysstat_read(&sysstat);

    static struct table_column_t columns[3];
    columns[0].width = (SCREEN_CLIENT_AREA_RIGHT - SCREEN_CLIENT_AREA_LEFT) - (64+35);
    columns[0].alignment = TABLE_ALIGN_LEFT;
    columns[1].width = 64;
    columns[1].alignment = TABLE_ALIGN_RIGHT;
    columns[2].width = 25;
    columns[2].alignment = TABLE_ALIGN_LEFT;

    lpcd_table_start(
        comm,
        x0, y0+14,
        14, columns, 3);

    static const char *header = "Statistics\0\0";

    lpcd_table_row(
        comm,
        LPC_FONT_DEJAVU_SANS_12PX_BF,
        0x0000, 0xffff,
        header, 13);

    char buffer[STAT_TABLE_FORMATTER_SIZE];
    size_t len;

    struct table_row_formatter_t formatter;
    table_row_formatter_init(
        &formatter,
        buffer,
        STAT_TABLE_FORMATTER_SIZE);

    table_row_formatter_reset(&formatter);
    table_row_formatter_append(
        &formatter,
        "Max. resident set size");
    table_row_formatter_append(
        &formatter, "%d", sysstat.maxrss);
    table_row_formatter_append(
        &formatter, " kB");
    table_row_formatter_get(&formatter, &len);
    lpcd_table_row(
        comm,
        LPC_FONT_DEJAVU_SANS_12PX,
        0x0000, 0xffff,
        buffer, len);

    table_row_formatter_reset(&formatter);
    table_row_formatter_append(
        &formatter,
        "Physical memory: free");
    table_row_formatter_append(
        &formatter, "%d", sysstat.mem_free);
    table_row_formatter_append(
        &formatter, " kB");
    table_row_formatter_get(&formatter, &len);
    lpcd_table_row(
        comm,
        LPC_FONT_DEJAVU_SANS_12PX,
        0x0000, 0xffff,
        buffer, len);

    table_row_formatter_reset(&formatter);
    table_row_formatter_append(
        &formatter,
        "Physical memory: in use");
    table_row_formatter_append(
        &formatter, "%.2f",
        (1 - (float)sysstat.mem_free / sysstat.mem_total) * 100);
    table_row_formatter_append(
        &formatter, " %%");
    table_row_formatter_get(&formatter, &len);
    lpcd_table_row(
        comm,
        LPC_FONT_DEJAVU_SANS_12PX,
        0x0000, 0xffff,
        buffer, len);

    table_row_formatter_reset(&formatter);
    table_row_formatter_append(
        &formatter,
        "Swap: free");
    table_row_formatter_append(
        &formatter, "%d", sysstat.swap_free);
    table_row_formatter_append(
        &formatter, " kB");
    table_row_formatter_get(&formatter, &len);
    lpcd_table_row(
        comm,
        LPC_FONT_DEJAVU_SANS_12PX,
        0x0000, 0xffff,
        buffer, len);

    table_row_formatter_reset(&formatter);
    table_row_formatter_append(
        &formatter,
        "Swap: in use");
    table_row_formatter_append(
        &formatter, "%.2f",
        (1 - (float)sysstat.swap_free / sysstat.swap_total) * 100);
    table_row_formatter_append(
        &formatter, " %%");
    table_row_formatter_get(&formatter, &len);
    lpcd_table_row(
        comm,
        LPC_FONT_DEJAVU_SANS_12PX,
        0x0000, 0xffff,
        buffer, len);

    table_row_formatter_free(&formatter);

}

/* implementation */

void screen_misc_free(struct screen_t *screen)
{

}

void screen_misc_hide(struct screen_t *screen)
{
    broker_remove_task_func(screen->broker, &regular_update);
}

void screen_misc_init(struct screen_t *screen, struct xmpp_t *xmpp)
{
    screen->show = &screen_misc_show;
    screen->hide = &screen_misc_hide;
    screen->repaint = &screen_misc_repaint;
    screen->free = &screen_misc_free;
    screen->touch = &screen_misc_touch;

    struct screen_misc_t *misc = malloc(sizeof(struct screen_misc_t));
    misc->xmpp = xmpp;

    screen->private = misc;
}

void screen_misc_repaint(struct screen_t *screen)
{
    sysstat_table(
        screen->comm,
        SCREEN_CLIENT_AREA_LEFT,
        SCREEN_CLIENT_AREA_TOP);

    status_table(
        screen->broker,
        SCREEN_CLIENT_AREA_LEFT,
        SCREEN_CLIENT_AREA_TOP+6*14+4);
}

void screen_misc_touch(struct screen_t *screen,
                       coord_int_t xc,
                       coord_int_t yc,
                       coord_int_t z)
{

}

void screen_misc_show(struct screen_t *screen)
{
    broker_enqueue_new_task_in(
        screen->broker,
        &regular_update,
        UPDATE_INTERVAL,
        screen);
}
