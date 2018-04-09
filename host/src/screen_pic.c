#include "screen_pic.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "private_config.h"
#include "broker.h"
#include "lpcdisplay.h"
#include "common/comm_lpc1114.h"

#define DRAW_INTERVAL (10)

static const char *picfiles[] = SCREEN_PIC_FILES;


// intentionally swapped due to draw order
#define PIC_WIDTH (SCREEN_CLIENT_AREA_HEIGHT)
#define PIC_HEIGHT (SCREEN_CLIENT_AREA_WIDTH)
#define PIC_NREC (PIC_WIDTH*PIC_HEIGHT)
#define PIC_RECSIZE (2)

#define DRAWCALLS_PER_ROUND ((PIC_WIDTH * 4 + IMAGE_DATA_CHUNK_LENGTH - 1) / IMAGE_DATA_CHUNK_LENGTH)


struct screen_pic_t {
    bool task_scheduled;
    uint16_t pixel;
    int last_chosen_on;
    uint16_t *current_picture_data;
};


#define SCREEN_PIC_FILE_COUNT (sizeof(picfiles) / sizeof(const char *))

static bool draw_step(
        struct broker_t *broker,
        struct timespec *next_run,
        void *userdata)
{
    struct screen_pic_t *picscreen = (struct screen_pic_t *)userdata;

    if (broker->active_screen != SCREEN_PIC) {
        picscreen->task_scheduled = false;
        return false;
    }

    if (picscreen->pixel >= PIC_NREC) {
        picscreen->task_scheduled = false;
        return false;
    }

    timestamp_gettime_in_future(
        next_run, DRAW_INTERVAL);

    int p0 = picscreen->pixel;
    int l0 = p0 / PIC_WIDTH;
    lpcd_image_start(broker->comm,
                     SCREEN_CLIENT_AREA_LEFT + l0,
                     SCREEN_CLIENT_AREA_TOP,
                     SCREEN_CLIENT_AREA_RIGHT - 1,
                     SCREEN_CLIENT_AREA_BOTTOM - 2);

    fprintf(stderr, "picscreen: p0 = %d (ndrawcalls = %lu, chunk length = %lu)\n",
            p0,
            DRAWCALLS_PER_ROUND,
            IMAGE_DATA_CHUNK_LENGTH);
    for (unsigned i = 0; i < DRAWCALLS_PER_ROUND; ++i) {
        if (p0 >= PIC_NREC) {
            break;
        }

        int p1 = p0 + IMAGE_DATA_CHUNK_LENGTH;
        if (p1 >= PIC_NREC) {
            p1 = PIC_NREC;
        }

        lpcd_image_data(broker->comm,
                        &picscreen->current_picture_data[p0],
                        (p1 - p0)*sizeof(uint16_t));

        p0 = p1;
    }
    fprintf(stderr, "picscreen: new p0 = %d (PIC_WIDTH = %d)\n", p0,
            PIC_WIDTH);
    picscreen->pixel = (p0 / PIC_WIDTH) * PIC_WIDTH;

    return true;
}

static void choose_picture(struct screen_pic_t *picscreen)
{
    time_t timep = time(NULL);
    struct tm *time = localtime(&timep);
    picscreen->pixel = 0;
    if (picscreen->current_picture_data != NULL &&
            time->tm_mday == picscreen->last_chosen_on)
    {
        return;
    }

    const char *const filename = picfiles[0];  // FIXME
    FILE *fh = fopen(filename, "r");
    if (fh == NULL) {
        fprintf(stderr,
                "picscreen: failed to open %s (%s)\n",
                filename,
                strerror(errno));
        return;
    }

    memset(&picscreen->current_picture_data[0], 0, PIC_NREC*PIC_RECSIZE);
    const size_t nread = fread(
                &picscreen->current_picture_data[0],
            PIC_RECSIZE, PIC_NREC,
            fh);
    if (nread < PIC_NREC) {
        fprintf(stderr, "picscreen: %s has fewer pixels than expected (%zu < %d)",
                filename, nread, PIC_NREC);
    }

    fclose(fh);
}

void screen_pic_free(struct screen_t *screen)
{
    free(screen->private);
}

void screen_pic_hide(struct screen_t *screen)
{

}

void screen_pic_init(struct screen_t *screen)
{
    fprintf(stderr, "picscreen: w = %d, h = %d\n",
            PIC_WIDTH, PIC_HEIGHT);

    screen->hide = &screen_pic_hide;
    screen->show = &screen_pic_show;
    screen->repaint = &screen_pic_repaint;
    screen->free = &screen_pic_free;

    struct screen_pic_t *picscreen = malloc(sizeof(struct screen_pic_t));
    picscreen->task_scheduled = false;
    picscreen->current_picture_data = malloc(PIC_NREC*PIC_RECSIZE);
    memset(&picscreen->current_picture_data[0], 0, PIC_NREC*PIC_RECSIZE);
    picscreen->pixel = 0;

    screen->private = picscreen;
}

void screen_pic_repaint(struct screen_t *screen)
{

}

void screen_pic_show(struct screen_t *screen)
{
    struct screen_pic_t *picscreen = (struct screen_pic_t *)screen->private;
    if (!picscreen->task_scheduled) {
        broker_enqueue_new_task_in(
                    screen->broker,
                    draw_step,
                    0,
                    screen->private);
        picscreen->task_scheduled = true;
    }

    choose_picture(screen->private);
}
