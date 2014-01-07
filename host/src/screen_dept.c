#include "screen_dept.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "common/comm_lpc1114.h"

#include "departure.h"
#include "lpcdisplay.h"
#include "utils.h"

/* utilities */

static inline void deptarture_paint(
    struct screen_t *screen,
    struct screen_dept_t *dept)
{
    static struct table_column_t columns[3];
    columns[0].width = 28;
    columns[0].alignment = TABLE_ALIGN_LEFT;
    columns[1].width = 120;
    columns[1].alignment = TABLE_ALIGN_LEFT;
    columns[2].width = 28;
    columns[2].alignment = TABLE_ALIGN_RIGHT;

    static const char* header = "L#\0Fahrtziel\0min";

    //~ lpcd_fill_rectangle(
        //~ screen->comm,
        //~ SCREEN_CLIENT_AREA_LEFT, SCREEN_CLIENT_AREA_TOP,
        //~ SCREEN_CLIENT_AREA_LEFT+180, SCREEN_CLIENT_AREA_BOTTOM,
        //~ 0xffff);

    lpcd_table_start(
        screen->comm,
        SCREEN_CLIENT_AREA_LEFT, SCREEN_CLIENT_AREA_TOP+14,
        14, columns, 3);

    lpcd_table_row(
        screen->comm,
        LPC_FONT_DEJAVU_SANS_12PX_BF,
        0x0000, 0xffff,
        header, 17);

    char *buffer = NULL;
    intptr_t buflen = 0;

    int len = array_length(&dept->rows);
    if (len > MAX_DEPT_ROWS) {
        len = MAX_DEPT_ROWS;
    }
    for (int i = 0; i < len; i++) {
        struct dept_row_t *row = array_get(&dept->rows, i);

        const intptr_t required_length =
            strlen(row->lane) + 1 +
            strlen(row->destination) + 1 +
            5 + // fixed length for eta
            1 // NUL
            ;

        if (required_length > buflen) {
            char *new_buffer = realloc(buffer, required_length);
            if (!new_buffer) {
                free(buffer);
                panicf("screen_dept_repaint: out of memory\n");
            }
            buflen = required_length;
            buffer = new_buffer;
        }

        char *pos = buffer;
        intptr_t remlength = buflen;
        intptr_t total_length = 0;
        int written = snprintf(
            pos, remlength,
            "%s", (char*)row->lane)+1;
        assert(written < remlength);
        pos += written;
        total_length += written;
        remlength -= written;

        written = snprintf(
            pos, remlength,
            "%s", row->destination)+1;
        assert(written < remlength);
        pos += written;
        total_length += written;
        remlength -= written;

        if (row->eta < -9) {
            written = snprintf(
                pos, remlength,
                "-∞")+1;
        } else if (row->eta > 999) {
            written = snprintf(
                pos, remlength,
                "+∞")+1;
        } else {
            written = snprintf(
                pos, remlength,
                "%d", row->eta)+1;
        }
        assert(written < remlength);
        total_length += written;

        lpcd_table_row(
            screen->comm,
            LPC_FONT_DEJAVU_SANS_12PX,
            0x0000, 0xffff,
            buffer, total_length);
    }

    free(buffer);

    static const char *empty = "\0\0";
    for (int i = array_length(&dept->rows); i < MAX_DEPT_ROWS; i++) {
        // fill with empty buffer
        lpcd_table_row(
            screen->comm,
            LPC_FONT_DEJAVU_SANS_12PX,
            0x0000, 0xffff,
            empty, 3);
    }

    lpcd_table_end(screen->comm);
}

/* implementation */

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
        struct dept_row_t *row = array_get(&dept->rows, i);
        free(row->destination);
        free(row);
    }
    array_free(&dept->rows);
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
    dept->status = REQUEST_STATUS_SUCCESS;

    screen->private = dept;
}

void screen_dept_repaint(struct screen_t *screen)
{
    struct screen_dept_t *dept = screen->private;

    switch (dept->status) {
    case REQUEST_STATUS_TIMEOUT:
    {
        screen_draw_background(screen);
        lpcd_draw_text(
            screen->comm,
            LPC_FONT_DEJAVU_SANS_12PX_BF,
            SCREEN_CLIENT_AREA_LEFT,
            SCREEN_CLIENT_AREA_TOP+14,
            0x0000,
            "Data request timeouted");
        break;
    }
    case REQUEST_STATUS_ERROR:
    {
        screen_draw_background(screen);
        lpcd_draw_text(
            screen->comm,
            SCREEN_CLIENT_AREA_LEFT,
            SCREEN_CLIENT_AREA_TOP+14,
            LPC_FONT_DEJAVU_SANS_12PX_BF,
            0x0000,
            "Request error");
        break;
    }
    case REQUEST_STATUS_DISCONNECTED:
    {
        screen_draw_background(screen);
        lpcd_draw_text(
            screen->comm,
            LPC_FONT_DEJAVU_SANS_12PX_BF,
            SCREEN_CLIENT_AREA_LEFT,
            SCREEN_CLIENT_AREA_TOP+14,
            0x0000,
            "Disconnect during request");
        break;
    }
    case REQUEST_STATUS_SUCCESS:
    {
        deptarture_paint(screen, dept);
        break;
    }
    }
}

void screen_dept_set_error(
    struct screen_t *screen,
    enum xmpp_request_status_t status)
{
    struct screen_dept_t *dept = screen->private;
    dept->status = status;
}

void screen_dept_show(struct screen_t *screen)
{
    screen_draw_background(screen);
}

void screen_dept_update_data(
    struct screen_t *screen,
    struct array_t *new_data)
{
    struct screen_dept_t *dept = screen->private;
    dept->status = REQUEST_STATUS_SUCCESS;
    array_swap(&dept->rows, new_data);
    while (!array_empty(new_data)) {
        struct dept_row_t *row = array_pop(new_data, -1);
        free(row->destination);
        free(row);
    }
}
