#include "broker.h"

#include "common/comm_arduino.h"
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
#include "private_config.h"
#include "sensor.h"

#include "screen_dept.h"
#include "screen_weather.h"
#include "screen_net.h"
#include "screen_misc.h"

#define TABBAR_LEFT ((LCD_WIDTH-1)-SCREEN_MARGIN_RIGHT)
#define TABBAR_TOP (SCREEN_CLIENT_AREA_TOP+4)

static const uint8_t board_sensor[7] = {0x28,
                                        0x7c, 0xc2, 0x52, 0x04, 0x00, 0x00};
static const uint8_t hall_sensor[7] = {0x28,
                                       0xe1, 0x89, 0x02, 0x04, 0x00, 0x00};

/* helper functions */

bool task_less(
    struct task_t *const task_a,
    struct task_t *const task_b)
{
    return timestamp_less(
        &task_a->run_at, &task_b->run_at);
}

bool batch_less(
    struct sensor_readout_batch_t *const batch_a,
    struct sensor_readout_batch_t *const batch_b)
{
    // items are already sorted by time
    return batch_a->data[0].readout_time < batch_b->data[0].readout_time;
}


bool broker_departure_request(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata);
void broker_departure_response(
    struct xmpp_t *xmpp,
    struct array_t *result,
    void *const userdata,
    enum xmpp_request_status_t status);
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
bool broker_is_asleep(struct broker_t *broker);
void broker_process_lpc_message(
    struct broker_t *broker, struct lpc_msg_t *msg);
void broker_process_comm_message(struct broker_t *broker, void *item);
void broker_process_xmpp_message(
    struct broker_t *broker,
    struct xmpp_queue_item_t *item);
void broker_repaint_screen(
    struct broker_t *broker);
void broker_repaint_tabbar(
    struct broker_t *broker);
void broker_repaint_time(
    struct broker_t *broker);
void broker_reset_sleepout_timer(
    struct broker_t *broker);
void broker_run_next_task(struct broker_t *broker);
void broker_sensor_submission_response(
    struct xmpp_t *xmpp,
    struct sensor_readout_batch_t *batch,
    void *const userdata,
    enum xmpp_request_status_t status);
bool broker_sleep_timer(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata);
void broker_submit_sensor_data(
    struct broker_t *broker,
    const uint8_t sensor_id[7],
    const int16_t raw_value);
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
void broker_wake_up(
    struct broker_t *broker);
bool broker_weather_request(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata);
void broker_weather_response(
    struct xmpp_t *xmpp,
    struct array_t *result,
    void *const userdata,
    enum xmpp_request_status_t status);


bool broker_departure_request(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata)
{
    timestamp_gettime_in_future(next_run, 30000);
    if (!xmppintf_is_available(broker->xmpp)) {
        return false;
    }
    xmppintf_request_departure_data(
        broker->xmpp,
        &broker_departure_response,
        broker);
    return true;
}

void broker_departure_response(
    struct xmpp_t *const xmpp,
    struct array_t *result,
    void *const userdata,
    enum xmpp_request_status_t status)
{
    struct broker_t *broker = userdata;

    pthread_mutex_lock(&broker->screen_mutex);
    switch (status)
    {
    case REQUEST_STATUS_TIMEOUT:
    case REQUEST_STATUS_ERROR:
    case REQUEST_STATUS_DISCONNECTED:
    {
        fprintf(stderr,
                "broker: departure response is negative: %d\n",
                status);
        screen_dept_set_error(
            &broker->screens[SCREEN_BUS_MONITOR],
            status);
        break;
    }
    case REQUEST_STATUS_SUCCESS:
    {
        screen_dept_update_data(
            &broker->screens[SCREEN_BUS_MONITOR],
            result);
        break;
    }
    }

    if ((broker->active_screen == SCREEN_BUS_MONITOR) &&
        comm_is_available(broker->comm))
    {
        screen_repaint(
            &broker->screens[SCREEN_BUS_MONITOR]);
    }
    pthread_mutex_unlock(&broker->screen_mutex);

    if (result) {
        array_free(result);
        free(result);
    }
}

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
    /* fprintf(stderr, "debug: broker: new task %p\n", task); */
    heap_insert(&broker->tasks, task);
}

void broker_free(struct broker_t *broker)
{
    fprintf(stderr, "debug: broker: free\n");
    broker->terminated = true;
    pthread_join(broker->thread, NULL);

    pthread_mutex_destroy(&broker->screen_mutex);
    pthread_mutex_destroy(&broker->activity_mutex);
    pthread_mutex_destroy(&broker->sensor.mutex);

    for (int i = 0;
         i < array_length(&broker->tasks.array);
         ++i)
    {
        struct task_t *task = array_get(&broker->tasks.array, i);
        free(task);
    }
    heap_free(&broker->tasks);

    for (int i = 0; i < SCREEN_COUNT; i++)
    {
        screen_free(&broker->screens[i]);
    }
    fprintf(stderr, "debug: broker: freed completely\n");
}

struct task_t *broker_get_next_task(struct broker_t *broker)
{
    if (heap_length(&broker->tasks) == 0) {
        return NULL;
    }

    return heap_get_min(&broker->tasks);
}

void broker_handle_touch_down(
    struct broker_t *broker,
    coord_int_t x, coord_int_t y)
{
    broker->touch_is_up = false;
    if (broker_is_asleep(broker)) {
        broker_wake_up(broker);
        return;
    }
    int new_screen = broker_tab_hit_test(broker, x, y);
    if ((new_screen != -1) && (new_screen != broker->active_screen)) {
        broker_switch_screen(broker, new_screen);
    }
    broker_reset_sleepout_timer(broker);
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
    broker_reset_sleepout_timer(broker);
}

void broker_init(
    struct broker_t *broker,
    struct comm_t *comm,
    struct xmpp_t *xmpp)
{
    broker->terminated = false;
    broker->comm = comm;
    broker->xmpp = xmpp;
    broker->touch_is_up = true;
    broker->asleep = false;
    broker->active_screen = 0;
    timestamp_gettime(&broker->last_activity);
    heap_init(&broker->tasks, 32, (heap_less_t)&task_less);

    screen_create(
        &broker->screens[SCREEN_BUS_MONITOR],
        broker,
        "DVB Abfahrtsmonitor",
        "DVB");
    screen_dept_init(&broker->screens[SCREEN_BUS_MONITOR]);

    screen_create(
        &broker->screens[SCREEN_WEATHER_INFO],
        broker,
        "Wetterdaten",
        "Enviro");
    screen_weather_init(&broker->screens[SCREEN_WEATHER_INFO]);

    screen_create(
        &broker->screens[SCREEN_NET],
        broker,
        "Netzwerk",
        "Net");
    screen_net_init(&broker->screens[SCREEN_NET]);

    screen_create(
        &broker->screens[SCREEN_MISC],
        broker,
        "Systeminformationen",
        "Misc");
    screen_misc_init(&broker->screens[SCREEN_MISC]);

    array_init(&broker->sensor.all_batches, MAX_BATCHES);
    array_init(&broker->sensor.free_batches, MAX_BATCHES / 2);
    heap_init(&broker->sensor.full_batches,
              MAX_BATCHES / 2,
              (heap_less_t)&batch_less);
    broker->sensor.curr_batch = NULL;

    pthread_mutex_init(&broker->screen_mutex, NULL);
    pthread_mutex_init(&broker->activity_mutex, NULL);
    pthread_mutex_init(&broker->sensor.mutex, NULL);
    pthread_create(
        &broker->thread, NULL,
        (void*(*)(void*))&broker_thread,
        broker);
}

bool broker_is_asleep(struct broker_t *broker)
{
    bool result;
    pthread_mutex_lock(&broker->activity_mutex);
    result = broker->asleep;
    pthread_mutex_unlock(&broker->activity_mutex);
    return result;
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

void broker_process_arduino_message(
    struct broker_t *broker, struct ard_msg_t *msg)
{
    switch (msg->subject) {
    case ARD_SUBJECT_SENSOR_READOUT:
    {
        uint8_t sensor_id[7];
        memcpy(&sensor_id[0],
               &msg->data.sensor_readout.sensor_id[0], 7);
        const int16_t raw_temperature = le16toh(
            msg->data.sensor_readout.raw_readout);

        /* fprintf(stderr, "broker: debug: received sensor readout: sensor_id="); */
        /* for (int i = 0; i < 7; i++) { */
        /*     fprintf(stderr, "%02x", sensor_id[i]); */
        /* } */
        /* fprintf(stderr, "; raw_value=%04x; value=%.2f\n", */
        /*         raw_temperature, */
        /*         raw_temperature/16.); */

        broker_submit_sensor_data(broker,
                                  sensor_id,
                                  raw_temperature);

        break;
    }
    default:
    {
        fprintf(stderr, "broker: unknown subject in arduino message: %d\n",
                msg->subject);
        break;
    }
    }
}

void broker_process_comm_message(struct broker_t *broker, void *item)
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
        struct ard_msg_t msg;
        memcpy(&msg, &((uint8_t*)item)[sizeof(struct msg_header_t)],
               HDR_GET_PAYLOAD_LENGTH(*hdr));
        free(item);
        broker_process_arduino_message(broker, &msg);
        return;
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

void broker_process_xmpp_message(
    struct broker_t *broker,
    struct xmpp_queue_item_t *item)
{
    switch (item->type)
    {
    case XMPP_DEPARTURE_DATA:
    {
        fprintf(stderr,
                "xmpp: ignored legacy public transport message\n");
        break;
    }
    default:
    {
        panicf("broker: unknown xmpp message type %d\n",
               item->type);
    }
    }
}

static inline void broker_repaint_screen_nolock(
    struct broker_t *broker)
{
    if (broker->active_screen >= 0) {
        struct screen_t *screen = &broker->screens[broker->active_screen];
        screen_draw_header(screen);
        screen_draw_background(screen);
        screen_repaint(screen);
    }
}

void broker_repaint_screen(
    struct broker_t *broker)
{
    pthread_mutex_lock(&broker->screen_mutex);
    broker_repaint_screen_nolock(broker);
    pthread_mutex_unlock(&broker->screen_mutex);
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
        LPC_FONT_CANTARELL_20PX_BF, 0xffff,
        buffer);
}

void broker_reset_sleepout_timer(struct broker_t *broker)
{
    pthread_mutex_lock(&broker->activity_mutex);
    if (broker->asleep) {
        pthread_mutex_unlock(&broker->activity_mutex);
        broker_wake_up(broker);
        return;
    }
    timestamp_gettime(&broker->last_activity);
    pthread_mutex_unlock(&broker->activity_mutex);
}

void broker_run_next_task(struct broker_t *broker)
{
    struct task_t *task = heap_pop_min(&broker->tasks);
    /* fprintf(stderr, "debug: broker: running task %p\n", task); */
    bool run_again = task->func(broker, &task->run_at, task->userdata);
    if (!run_again) {
        free(task);
        return;
    }
    broker_enqueue_task(broker, task);
}

void broker_sensor_submission_response(
    struct xmpp_t *xmpp,
    struct sensor_readout_batch_t *batch,
    void *const userdata,
    enum xmpp_request_status_t status)
{
    struct broker_t *broker = userdata;

    pthread_mutex_lock(&broker->sensor.mutex);
    switch (status)
    {
    case REQUEST_STATUS_TIMEOUT:
    case REQUEST_STATUS_ERROR:
    case REQUEST_STATUS_DISCONNECTED:
    {
        fprintf(stderr,
                "broker: sensor submission failed: %d, reenqueing buffer\n",
                status);
        heap_insert(&broker->sensor.full_batches, batch);
        break;
    }
    case REQUEST_STATUS_SUCCESS:
    {
        array_append(&broker->sensor.free_batches, batch);
        break;
    }
    }
    pthread_mutex_unlock(&broker->sensor.mutex);
}

void broker_submit_sensor_data(
    struct broker_t *broker,
    const uint8_t sensor_id[7],
    const int16_t raw_value)
{
    pthread_mutex_lock(&broker->sensor.mutex);

    if (broker->sensor.curr_batch == NULL) {
        if (array_length(&broker->sensor.free_batches) > 0) {
            broker->sensor.curr_batch = array_pop(
                &broker->sensor.free_batches, -1);
            broker->sensor.curr_batch->write_offset = 0;
        } else if (array_length(&broker->sensor.all_batches) < MAX_BATCHES) {
            broker->sensor.curr_batch = malloc(
                sizeof(struct sensor_readout_batch_t));
            if (!broker->sensor.curr_batch) {
                panicf("broker: out of memory while allocating sensor readout "
                       "batch\n");
            }
            array_append(&broker->sensor.all_batches, broker->sensor.curr_batch);
            broker->sensor.curr_batch->write_offset = 0;
        } else {
            fprintf(stderr, "broker: dropping sensor data, running out of "
                    "space\n");
            // do not exit here -- submission may free space later
        }
    }

    // space to write
    if (broker->sensor.curr_batch != NULL) {
        struct sensor_readout_batch_t *batch = broker->sensor.curr_batch;
        struct sensor_readout_t *dest = &batch->data[batch->write_offset++];
        dest->readout_time = time(NULL);
        memcpy(&dest->sensor_id[0], &sensor_id[0], 7);
        dest->raw_value = raw_value;

        /* fprintf(stderr, "broker: debug: wrote %d out of %d in current batch\n", */
        /*         batch->write_offset, */
        /*         MAX_READOUTS_IN_BATCH); */

        if (batch->write_offset == MAX_READOUTS_IN_BATCH) {
            heap_insert(&broker->sensor.full_batches, batch);
            broker->sensor.curr_batch = NULL;
        }
    }

    if (memcmp(&sensor_id[0], &board_sensor[0], 7) == 0) {
        screen_weather_set_sensor(
            &broker->screens[SCREEN_WEATHER_INFO],
            SENSOR_EXTERIOR,
            raw_value);
    } else if (memcmp(&sensor_id[0], &hall_sensor[0], 7) == 0) {
        screen_weather_set_sensor(
            &broker->screens[SCREEN_WEATHER_INFO],
            SENSOR_INTERIOR,
            raw_value);
    }

    if (!xmppintf_weather_peer_is_available(broker->xmpp)) {
        pthread_mutex_unlock(&broker->sensor.mutex);
        return;
    }

    while (heap_length(&broker->sensor.full_batches) > 0) {
        xmppintf_submit_sensor_data(
            broker->xmpp,
            heap_pop_min(&broker->sensor.full_batches),
            &broker_sensor_submission_response,
            broker);
    }

    pthread_mutex_unlock(&broker->sensor.mutex);
}

void broker_switch_screen(
    struct broker_t *broker,
    int new_screen)
{
    pthread_mutex_lock(&broker->screen_mutex);
    if (broker->active_screen != -1) {
        struct screen_t *screen = &broker->screens[broker->active_screen];
        screen_hide(screen);
    }
    broker->active_screen = new_screen;
    if (broker->active_screen != -1) {
        screen_show(&broker->screens[broker->active_screen]);
        broker_repaint_screen_nolock(broker);
    }
    broker_repaint_tabbar(broker);
    pthread_mutex_unlock(&broker->screen_mutex);
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

void broker_remove_task_func(
    struct broker_t *broker,
    task_func_t func)
{
    for (intptr_t i = 0;
         i < array_length(&broker->tasks.array);
         i++)
    {
        struct task_t *task = array_get(&broker->tasks.array, i);
        if (task->func == func) {
            heap_delete(&broker->tasks, i);
            break;
        }
    }

}

bool broker_sleep_timer(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata)
{
    struct timespec now;

    pthread_mutex_lock(&broker->activity_mutex);
    if (broker->asleep) {
        pthread_mutex_unlock(&broker->activity_mutex);
        return false;
    }
    timestamp_gettime(&now);
    if (timestamp_delta_in_msec(&now, &broker->last_activity) >= SLEEPOUT_TIMER) {
        lpcd_lullaby(broker->comm);
        pthread_mutex_lock(&broker->screen_mutex);
        if (broker->active_screen >= 0) {
            struct screen_t *curr_screen = &broker->screens[broker->active_screen];
            screen_hide(curr_screen);
        }
        pthread_mutex_unlock(&broker->screen_mutex);
        broker->asleep = true;
        pthread_mutex_unlock(&broker->activity_mutex);
        return false;
    }
    pthread_mutex_unlock(&broker->activity_mutex);

    timestamp_add_msec(next_run, SLEEPOUT_TIMER_INTERVAL);
    return true;
}

void _broker_thread_handle_comm(
    struct broker_t *broker, int fd)
{
    char act = recv_char(fd);
    switch (act)
    {
    case COMM_PIPECHAR_MESSAGE:
    {
        if (queue_empty(&broker->comm->recv_queue)) {
            fprintf(stderr, "broker: BUG: comm recv trigger received, "
                            "but queue is empty!\n");
            return;
        }
        void *item = queue_pop(&broker->comm->recv_queue);
        assert(item != NULL);
        broker_process_comm_message(broker, item);
        break;
    }
    case COMM_PIPECHAR_FAILED:
    {
        fprintf(stderr, "broker: debug: comm failed.\n");
        // handler will remove itself upon it’s next call
        break;
    }
    case COMM_PIPECHAR_READY:
    {
        fprintf(stderr, "broker: debug: comm ready.\n");
        lpcd_state_reset(broker->comm);
        lpcd_wake_up(broker->comm);
        broker_repaint_screen(broker);
        broker_repaint_tabbar(broker);
        break;
    }
    default:
        panicf("unknown comm pipechar: %c\n", act);
    }
}

void _broker_thread_handle_xmpp(
    struct broker_t *broker, int fd)
{
    char act = recv_char(fd);
    switch (act)
    {
    case XMPPINTF_PIPECHAR_MESSAGE:
    {
        if (queue_empty(&broker->xmpp->recv_queue)) {
            fprintf(stderr, "broker: BUG: xmpp recv trigger received, "
                            "but queue is empty!\n");
            return;
        }
        void *item = queue_pop(&broker->xmpp->recv_queue);
        assert(item != NULL);
        broker_process_xmpp_message(broker, item);
        break;
    }
    case XMPPINTF_PIPECHAR_FAILED:
    {
        broker_remove_task_func(
            broker,
            &broker_weather_request);
        broker_remove_task_func(
            broker,
            &broker_departure_request);
        fprintf(stderr, "broker: debug: xmpp failed.\n");
        break;
    }
    case XMPPINTF_PIPECHAR_READY:
    {
        fprintf(stderr, "broker: debug: xmpp ready.\n");
        broker_enqueue_new_task_in(
            broker,
            &broker_weather_request,
            0,
            NULL);
        broker_enqueue_new_task_in(
            broker,
            &broker_departure_request,
            0,
            NULL);
        break;
    }
    default:
        panicf("unknown xmpp pipechar: %c\n", act);
    }
}

void *broker_thread(struct broker_t *state)
{
#define FD_COUNT 2
#define FD_RECV_COMM 0
#define FD_RECV_XMPP 1
    struct pollfd pollfds[FD_COUNT];
    pollfds[FD_RECV_COMM].fd = state->comm->recv_fd;
    pollfds[FD_RECV_COMM].events = POLLIN;
    pollfds[FD_RECV_COMM].revents = 0;
    pollfds[FD_RECV_XMPP].fd = state->xmpp->recv_fd;
    pollfds[FD_RECV_XMPP].events = POLLIN;
    pollfds[FD_RECV_XMPP].revents = 0;

    broker_enqueue_new_task_in(
        state, &broker_update_time, 0, NULL);
    broker_enqueue_new_task_in(
        state, &broker_sleep_timer, SLEEPOUT_TIMER_INTERVAL, NULL);

    while (!state->terminated)
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
            _broker_thread_handle_comm(
                state, pollfds[FD_RECV_COMM].fd);
        }
        if (pollfds[FD_RECV_XMPP].revents & POLLIN) {
            _broker_thread_handle_xmpp(
                state, pollfds[FD_RECV_XMPP].fd);
        }
    }

    return NULL;
}

bool broker_update_time(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata)
{
    timestamp_gettime_in_future(next_run, 1000);
    if (!comm_is_available(broker->comm) || broker_is_asleep(broker)) {
        return true;
    }
    broker_repaint_time(broker);
    return true;
}

void broker_wake_up(struct broker_t *broker)
{
    pthread_mutex_lock(&broker->activity_mutex);
    if (!broker->asleep) {
        pthread_mutex_unlock(&broker->activity_mutex);
        return;
    }
    timestamp_gettime(&broker->last_activity);
    broker->asleep = false;
    lpcd_wake_up(broker->comm);
    lpcd_set_brightness(broker->comm, 0x0fff);
    broker_enqueue_new_task_in(
        broker, &broker_sleep_timer, SLEEPOUT_TIMER_INTERVAL, NULL);

    // force full repaint
    pthread_mutex_lock(&broker->screen_mutex);
    if (broker->active_screen >= 0) {
        struct screen_t *curr_screen = &broker->screens[broker->active_screen];
        screen_hide(curr_screen);
        broker_repaint_screen_nolock(broker);
    }
    pthread_mutex_unlock(&broker->screen_mutex);

    pthread_mutex_unlock(&broker->activity_mutex);
}

bool broker_weather_request(
    struct broker_t *broker,
    struct timespec *next_run,
    void *userdata)
{
    timestamp_gettime_in_future(next_run, 15*60*1000);
    if (!xmppintf_is_available(broker->xmpp)) {
        return false;
    }
    xmppintf_request_weather_data(
        broker->xmpp,
        CONFIG_WEATHER_LAT,
        CONFIG_WEATHER_LON,
        &broker_weather_response,
        screen_weather_get_request_array(
            &broker->screens[SCREEN_WEATHER_INFO]),
        broker);
    return true;
}

void broker_weather_response(
    struct xmpp_t *const xmpp,
    struct array_t *result,
    void *const userdata,
    enum xmpp_request_status_t status)
{
    struct broker_t *broker = userdata;

    pthread_mutex_lock(&broker->screen_mutex);
    switch (status)
    {
    case REQUEST_STATUS_TIMEOUT:
    case REQUEST_STATUS_ERROR:
    case REQUEST_STATUS_DISCONNECTED:
    {
        fprintf(stderr,
                "broker: weather response is negative: %d\n",
                status);
        break;
    }
    case REQUEST_STATUS_SUCCESS:
    {
        screen_weather_update(
            &broker->screens[SCREEN_WEATHER_INFO]);
        if ((broker->active_screen == SCREEN_WEATHER_INFO) &&
            comm_is_available(broker->comm))
        {
            screen_repaint(
                &broker->screens[SCREEN_WEATHER_INFO]);
        }
        break;
    }
    }
    pthread_mutex_unlock(&broker->screen_mutex);

}
