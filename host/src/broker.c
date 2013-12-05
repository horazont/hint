#include "broker.h"

#include "common/comm_lpc1114.h"

#include <poll.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lpcdisplay.h"
#include "timestamp.h"
#include "utils.h"

#define TABBAR_LEFT ((LCD_WIDTH-1)-SCREEN_MARGIN_RIGHT)
#define TABBAR_TOP (SCREEN_CLIENT_AREA_TOP+4)

void broker_enqueue_new_task_at(
    struct broker_t *broker,
    task_func_t func,
    const struct timespec *run_at,
    void *userdata);
void broker_enqueue_new_task_in(
    struct broker_t *broker,
    task_func_t func,
    uint32_t in_msec,
    void *userdata);
void broker_enqueue_task(struct broker_t *broker, struct task_t *task);
struct task_t *broker_get_next_task(struct broker_t *broker);
void broker_handle_touch_down(
    struct broker_t *broker,
    coord_int_t x, coord_int_t y);
void broker_handle_touch_move(
    struct broker_t *broker,
    coord_int_t x, coord_int_t y);
void broker_handle_touch_up(
    struct broker_t *broker,
    coord_int_t x, coord_int_t y);
void broker_init(
    struct broker_t *broker,
    struct comm_t *comm,
    struct xmpp_t *xmpp);
void broker_process_lpc_message(
    struct broker_t *broker, struct lpc_msg_t *msg);
void broker_process_message(struct broker_t *broker, void *item);
void broker_repaint_screen(
    struct broker_t *broker);
void broker_repaint_tabbar(
    struct broker_t *broker);
void broker_repaint_time(
    struct broker_t *broker);
void broker_run_next_task(struct broker_t *broker);
void broker_switch_screen(
    struct broker_t *broker,
    int new_screen);
int broker_tab_hit_test(
    struct broker_t *broker,
    coord_int_t x, coord_int_t y);
void *broker_thread(struct broker_t *state);
bool broker_update_time(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata);


void broker_enqueue_new_task_at(
    struct broker_t *broker,
    task_func_t func,
    const struct timespec *run_at,
    void *userdata)
{
    struct task_t *new_task = malloc(sizeof(struct task_t));
    new_task->func = func;
    memcpy(&new_task->run_at, run_at, sizeof(struct timespec));
    new_task->userdata = userdata;
    broker_enqueue_task(broker, new_task);
}

void broker_enqueue_new_task_in(
    struct broker_t *broker,
    task_func_t func,
    uint32_t in_msec,
    void *userdata)
{
    struct timespec now;
    timestamp_gettime(&now);
    timestamp_add_msec(&now, in_msec);
    broker_enqueue_new_task_at(broker, func, &now, userdata);
}

void broker_enqueue_task(struct broker_t *broker, struct task_t *task)
{
    // FIXME: implement a binary search here
    for (intptr_t i = 0; i < array_length(&broker->tasks); i++) {
        const struct task_t *other_task = array_get(&broker->tasks, i);
        if (timestamp_less(
            &other_task->run_at,
            &task->run_at))
        {
            array_push(&broker->tasks, i, task);
            return;
        }
    }
    array_push(&broker->tasks, INTPTR_MAX, task);
}

struct task_t *broker_get_next_task(struct broker_t *broker)
{
    if (array_length(&broker->tasks) == 0) {
        return NULL;
    }

    return array_get(&broker->tasks, -1);
}

void broker_handle_touch_down(
    struct broker_t *broker,
    coord_int_t x, coord_int_t y)
{
    broker->touch_is_up = false;
    int new_screen = broker_tab_hit_test(broker, x, y);
    if ((new_screen != -1) && (new_screen != broker->active_screen)) {
        broker_switch_screen(broker, new_screen);
    }
}

void broker_handle_touch_move(
    struct broker_t *broker,
    coord_int_t x, coord_int_t y)
{

}

void broker_handle_touch_up(
    struct broker_t *broker,
    coord_int_t x, coord_int_t y)
{
    broker->touch_is_up = true;
}

void broker_init(
    struct broker_t *broker,
    struct comm_t *comm,
    struct xmpp_t *xmpp)
{
    broker->comm = comm;
    broker->xmpp = xmpp;
    broker->touch_is_up = true;
    broker->active_screen = 0;
    array_init(&broker->tasks, 32);

    screen_create(
        &broker->screens[SCREEN_BUS_MONITOR],
        comm,
        "DVB Abfahrtsmonitor",
        "DVB");
    screen_dept_init(&broker->screens[SCREEN_BUS_MONITOR]);

    screen_create(
        &broker->screens[SCREEN_WEATHER_INFO],
        comm,
        "Wetterdaten",
        "Enviro");
    screen_weather_init(&broker->screens[SCREEN_WEATHER_INFO]);

    pthread_create(
        &broker->thread, NULL,
        (void*(*)(void*))&broker_thread,
        broker);
}

void broker_process_lpc_message(
    struct broker_t *broker, struct lpc_msg_t *msg)
{
    switch (msg->subject) {
    case LPC_SUBJECT_TOUCH_EVENT:
    {
        const coord_int_t x = le16toh(msg->payload.touch_ev.x);
        const coord_int_t y = le16toh(msg->payload.touch_ev.y);
        const coord_int_t z = le16toh(msg->payload.touch_ev.z);
        if (broker->touch_is_up && (z > 0)) {
            broker_handle_touch_down(broker, x, y);
        } else if ((!broker->touch_is_up) && (z > 0)) {
            broker_handle_touch_move(broker, x, y);
        } else if ((!broker->touch_is_up) && (z == 0)) {
            broker_handle_touch_up(broker, x, y);
        }
        break;
    }
    default:
    {
        fprintf(stderr, "broker: unknown subject in lpc message: %d\n",
                        msg->subject);
        break;
    }
    }
}

void broker_process_message(struct broker_t *broker, void *item)
{
    struct msg_header_t *hdr = (struct msg_header_t *)item;
    switch (HDR_GET_SENDER(*hdr)) {
    case MSG_ADDRESS_HOST:
    {
        fprintf(stderr, "broker: received message from meself\n");
        break;
    }
    case MSG_ADDRESS_LPC1114:
    {
        struct lpc_msg_t msg;
        memcpy(&msg, &((uint8_t*)item)[sizeof(struct msg_header_t)],
               HDR_GET_PAYLOAD_LENGTH(*hdr));
        free(item);
        broker_process_lpc_message(broker, &msg);
        return;
    }
    case MSG_ADDRESS_ARDUINO:
    {
        fprintf(stderr, "broker: received message from arduino, cannot handle\n");
        break;
    }
    default:
    {
        fprintf(stderr, "broker: unknown sender address: %d\n",
                        HDR_GET_SENDER(*hdr));
        break;
    }
    }
    comm_dump_message(hdr);
    free(item);
}

void broker_repaint_screen(
    struct broker_t *broker)
{
    if (broker->active_screen >= 0) {
        struct screen_t *screen = &broker->screens[broker->active_screen];
        screen_draw_header(screen);
        screen_draw_background(screen);
        screen_repaint(screen);
    }
}

void broker_repaint_tabbar(
    struct broker_t *broker)
{
    const coord_int_t tab_x0 = TABBAR_LEFT;
    coord_int_t tab_y0 = TABBAR_TOP;

    for (int i = 0; i < SCREEN_COUNT; i++) {
        screen_draw_tab(
            broker->comm,
            broker->screens[i].tab_caption,
            tab_x0,
            tab_y0,
            (i == broker->active_screen));

        tab_y0 += TAB_HEIGHT + TAB_PADDING;
    }
}

void broker_repaint_time(
    struct broker_t *broker)
{
    time_t timep = time(NULL);
    struct tm *time = localtime(&timep);
    static char buffer[6];
    strftime(buffer, 6, "%H:%M", time);
    if (time->tm_sec % 2 == 0) {
        buffer[2] = ' ';
    }
    lpcd_fill_rectangle(
        broker->comm,
        CLOCK_POSITION_X, 0,
        LCD_WIDTH-1, CLOCK_POSITION_Y+2,
        0x0000);
    lpcd_draw_text(
        broker->comm,
        CLOCK_POSITION_X, CLOCK_POSITION_Y,
        LPC_FONT_DEJAVU_SANS_20PX_BF, 0xffff,
        buffer);
}

void broker_run_next_task(struct broker_t *broker)
{
    struct task_t *task = array_pop(&broker->tasks, -1);
    bool run_again = task->func(broker, &task->run_at, task->userdata);
    if (!run_again) {
        free(task);
        return;
    }
    broker_enqueue_task(broker, task);
}

void broker_switch_screen(
    struct broker_t *broker,
    int new_screen)
{
    if (broker->active_screen != -1) {
        struct screen_t *screen = &broker->screens[broker->active_screen];
        screen_hide(screen);
    }
    broker->active_screen = new_screen;
    if (broker->active_screen != -1) {
        broker_repaint_screen(broker);
    }
    broker_repaint_tabbar(broker);
}

int broker_tab_hit_test(
    struct broker_t *broker,
    coord_int_t x, coord_int_t y)
{
    y -= TABBAR_TOP;
    x -= TABBAR_LEFT;
    if ((x < 0) || (x >= TAB_WIDTH)) {
        return -1;
    }
    if (y < 0) {
        return -1;
    }

    int index = y / (TAB_HEIGHT+TAB_PADDING);
    if (y - index*(TAB_HEIGHT+TAB_PADDING) > TAB_HEIGHT) {
        return -1;
    }

    if (index >= SCREEN_COUNT) {
        return -1;
    }

    return index;
}

void *broker_thread(struct broker_t *state)
{
#define FD_COUNT 2
#define FD_RECV_COMM 0
#define FD_RECV_XMPP 1
    struct pollfd pollfds[FD_COUNT];
    pollfds[0].fd = state->comm->recv_fd;
    pollfds[0].events = POLLIN;
    pollfds[0].revents = 0;
    pollfds[1].fd = state->xmpp->recv_fd;
    pollfds[1].events = POLLIN;
    pollfds[1].revents = 0;

    broker_repaint_screen(state);
    broker_repaint_tabbar(state);
    broker_enqueue_new_task_in(
        state, &broker_update_time, 0, NULL);

    while (true)
    {
        int32_t timeout = -1;
        struct task_t *task = broker_get_next_task(state);
        while (task) {
            struct timespec curr_time;
            timestamp_gettime(&curr_time);
            timeout = timestamp_delta_in_msec(
                &task->run_at, &curr_time);
            if (timeout > 0) {
                break;
            } else {
                broker_run_next_task(state);
            }
            task = broker_get_next_task(state);
        }

        poll(&pollfds[0], FD_COUNT, timeout);

        if (pollfds[FD_RECV_COMM].revents & POLLIN) {
            char act = recv_char(pollfds[FD_RECV_COMM].fd);
            if (queue_empty(&state->comm->recv_queue)) {
                fprintf(stderr, "broker: BUG: recv trigger received, "
                                "but queue is empty!\n");
                continue;
            }
            void *item = queue_pop(&state->comm->recv_queue);
            assert(item != NULL);
            broker_process_message(state, item);
        }
        if (pollfds[FD_RECV_XMPP].revents & POLLIN) {
            char act = recv_char(pollfds[FD_RECV_XMPP].fd);
            fprintf(stderr, "broker: received trigger from xmpp\n");
        }
    }

    return NULL;
}

bool broker_update_time(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata)
{
    broker_repaint_time(broker);
    timestamp_gettime(next_run);
    timestamp_add_msec(next_run, 1000);
    return true;
}
