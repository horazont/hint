#include "xmppintf.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#include "utils.h"
#include "weather.h"
#include "timestamp.h"

#include "config.h"

/* temporary structures for iq callback */

struct weather_callback_t {
    weather_callback_t callback;
    struct array_t *data;
    void *userdata;
};

void weather_callback_free(struct weather_callback_t *cb)
{
    free(cb);
}

struct weather_callback_t *weather_callback_new(
    weather_callback_t callback,
    struct array_t *data,
    void *userdata)
{
    struct weather_callback_t *cb =
        malloc(sizeof(struct weather_callback_t));

    if (!cb) {
        panicf("weather_callback_new: out of memory\n");
    }

    cb->callback = callback;
    cb->data = data;
    cb->userdata = userdata;

    return cb;
}

struct departure_callback_t {
    departure_callback_t callback;
    void *userdata;
};

void departure_callback_free(struct departure_callback_t *cb)
{
    free(cb);
}

struct departure_callback_t *departure_callback_new(
    departure_callback_t callback,
    void *userdata)
{
    struct departure_callback_t *cb =
        malloc(sizeof(struct departure_callback_t));

    if (!cb) {
        panicf("departure_callback_new: out of memory\n");
    }

    cb->callback = callback;
    cb->userdata = userdata;

    return cb;
}

struct sensor_submission_callback_t {
    sensor_submission_callback_t callback;
    struct sensor_readout_batch_t *batch;
    void *userdata;
};

void sensor_submission_callback_free(struct sensor_submission_callback_t *cb)
{
    free(cb);
}

struct sensor_submission_callback_t *sensor_submission_callback_new(
    sensor_submission_callback_t callback,
    struct sensor_readout_batch_t *batch,
    void *userdata)
{
    struct sensor_submission_callback_t *cb =
        malloc(sizeof(struct sensor_submission_callback_t));

    if (!cb) {
        panicf("sensor_submission_callback_new: out of memory\n");
    }

    cb->callback = callback;
    cb->batch = batch;
    cb->userdata = userdata;

    return cb;
}

/* iq callback handling */

typedef void (*iq_response_callback_t)(
    struct xmpp_t *const xmpp,
    xmpp_stanza_t *const stanza,
    void *const userdata,
    enum xmpp_request_status_t status);

struct iq_callback_t {
    char *id;
    struct timespec timeout_at;
    iq_response_callback_t on_response;
    void *userdata;
};

void iq_callback_free(struct iq_callback_t *cb)
{
    free(cb->id);
    free(cb);
}

struct iq_callback_t *iq_callback_new(
    const char *id,
    iq_response_callback_t on_response,
    int32_t timeout,
    void *userdata)
{
    struct iq_callback_t *cb = malloc(sizeof(struct iq_callback_t));
    if (!cb) {
        panicf("new_iq_callback: out of memory\n");
    }

    cb->id = strdup(id);
    if (!cb->id) {
        panicf("new_iq_callback: out of memory\n");
    }

    cb->on_response = on_response;
    cb->userdata = userdata;
    timestamp_gettime_in_future(
        &cb->timeout_at,
        timeout);

    return cb;
}

bool iq_callback_less(
    struct iq_callback_t *const a,
    struct iq_callback_t *const b)
{
    return timestamp_less(&a->timeout_at, &b->timeout_at);
}

/* end of iq callback handling */

const char *xmppintf_ns_public_transport = "https://xmlns.zombofant.net/xmpp/public-transport";
/*
 * in an iq set, this defines the interval at which push updates shall
 * be sent
 * <departure xmlns=xmppintf_ns_public_transport>
 *   <interval>30</interval>
 * </departure>
 *
 * in an iq result to a set operation, this returns the actual interval
 * which has been configured
 * <departure xmlns=xmppintf_ns_public_transport>
 *   <interval>30</interval>
 * </departure>
 *
 * in an iq get, this asks the remote to provide current departure data
 * <departure xmlns=xmppintf_ns_public_transport>
 *   <data />
 * </departure>
 *
 * in an iq result to a get or in an iq set push message, this carries
 * the information
 * <departure xmlns=xmppintf_ns_public_transport>
 *   <data>
 *     <!-- @eta is the authorative information (extrapolated if no
 *          current data has been requested) -->
 *     <dt lane="62" destination="Löbtau Süd" eta="3.12"/>
 *     <dt lane="85" destination="Löbtau Süd" eta="4.2"/>
 *   </data>
 * </departure>
 *
 * (a set push message must be replied to with an empty result message)
 *
 */

const char *xmppintf_ns_meteo_service = "https://xmlns.zombofant.net/xmpp/meteo-service";
const char *xmppintf_ns_sensor = "https://xmlns.zombofant.net/xmpp/sensor";

/* namespace tag names */

const char *xml_meteo_data = "data";
const char *xml_meteo_sources = "sources";
const char *xml_meteo_source = "source";
const char *xml_meteo_location = "l";
const char *xml_meteo_interval = "i";
const char *xml_meteo_point_data = "pd";
const char *xml_meteo_temperature = "t";
const char *xml_meteo_cloudcoverage = "cc";
const char *xml_meteo_pressure = "press";
const char *xml_meteo_fog = "f";
const char *xml_meteo_humidity = "h";
const char *xml_meteo_wind_direction = "wd";
const char *xml_meteo_wind_speed = "ws";
const char *xml_meteo_precipitation = "prec";
const char *xml_meteo_precipitation_probability = "precp";
const char *xml_meteo_attr_value = "v";
const char *xml_meteo_attr_min = "min";
const char *xml_meteo_attr_max = "max";
const char *xml_meteo_attr_temperature_type = "t";
const char *xml_meteo_attr_temperature_type_air = "air";
const char *xml_meteo_attr_cloudcoverage_level = "lvl";
const char *xml_meteo_attr_cloudcoverage_level_all = "all";
const char *xml_meteo_attr_interval_start = "start";
const char *xml_meteo_attr_interval_end = "end";
const char *xml_pt_data = "data";
const char *xml_pt_departure = "departure";
const char *xml_pt_departure_time = "dt";
const char *xml_pt_attr_departure_time_eta = "e";
const char *xml_pt_attr_departure_time_destination = "d";
const char *xml_pt_attr_departure_time_lane = "l";
const char *xml_pt_attr_departure_time_timestamp = "ts";
const char *xml_pt_attr_departure_time_dir = "dir";
const char *xml_sensor_data = "data";
const char *xml_sensor_point = "p";
const char *xml_sensor_attr_sensortype = "st";
const char *xml_sensor_attr_sensorid = "sid";
const char *xml_sensor_attr_readout_time = "t";
const char *xml_sensor_attr_raw_value = "rv";

const char *xmppintf_ns_ping = "urn:xmpp:ping";


/* utilities */

xmpp_stanza_t* iq(xmpp_ctx_t *const ctx, const char *type,
    const char *to, const char *id)
{
    xmpp_stanza_t *node = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(node, "iq");
    xmpp_stanza_set_type(node, type);
    if (id) {
        xmpp_stanza_set_attribute(node, "id", id);
    }
    if (to) {
        xmpp_stanza_set_attribute(node, "to", to);
    }
    return node;
}

xmpp_stanza_t *iq_error(
    xmpp_ctx_t *const ctx,
    xmpp_stanza_t *const in_reply_to,
    const char *type,
    const char *error_condition,
    const char *text)
{
    xmpp_stanza_t *iq_error = iq(
        ctx,
        "error",
        xmpp_stanza_get_attribute(in_reply_to, "from"),
        xmpp_stanza_get_attribute(in_reply_to, "id"));
    xmpp_stanza_t *error = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(error, "error");
    xmpp_stanza_set_attribute(error, "type", type);

    xmpp_stanza_t *forbidden = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(forbidden, error_condition);
    xmpp_stanza_set_ns(forbidden, "urn:ietf:params:xml:ns:xmpp-stanzas");
    xmpp_stanza_add_child(error, forbidden);
    xmpp_stanza_release(forbidden);

    if (text) {
        xmpp_stanza_t *text_cont = xmpp_stanza_new(ctx);
        xmpp_stanza_set_name(text_cont, "text");

        xmpp_stanza_t *text_node = xmpp_stanza_new(ctx);
        xmpp_stanza_set_text(text_node, text);
        xmpp_stanza_add_child(text_cont, text_node);
        xmpp_stanza_release(text_node);

        xmpp_stanza_add_child(error, text_cont);
        xmpp_stanza_release(text_cont);
    }

    xmpp_stanza_add_child(iq_error, error);
    xmpp_stanza_release(error);

    return iq_error;
}

void iq_reply_empty_result(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const in_reply_to)
{
    xmpp_stanza_t *result = iq(
        xmpp_conn_get_context(conn),
        "result",
        xmpp_stanza_get_attribute(in_reply_to, "from"),
        xmpp_stanza_get_id(in_reply_to));
    xmpp_send(conn, result);
    xmpp_stanza_release(result);
}

void add_text(
    xmpp_ctx_t *const ctx,
    xmpp_stanza_t *to_stanza,
    const char *text)
{
    xmpp_stanza_t *text_node = xmpp_stanza_new(ctx);
    xmpp_stanza_set_text(text_node, text);
    xmpp_stanza_add_child(to_stanza, text_node);
    xmpp_stanza_release(text_node);
}

static inline bool parse_isodate(
    const char *datestr,
    time_t *dest)
{
    struct tm tm;
    if (strptime(datestr, isodate_fmt, &tm) == NULL) {
        return false;
    }
    *dest = timegm(&tm);
    return true;
}

static inline bool parse_float(
    const char *floatstr,
    float *dest)
{
    char *endptr = NULL;
    *dest = strtof(floatstr, &endptr);
    if ((errno == ERANGE) || (endptr == floatstr)) {
        return false;
    }
    return true;
}

void xmppintf_handle_departure_reply(
    struct xmpp_t *const xmpp,
    xmpp_stanza_t *const stanza,
    void *const userdata,
    enum xmpp_request_status_t status);
int xmppintf_handle_iq_error(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata);
int xmppintf_handle_iq_timeout(
    xmpp_conn_t *const conn,
    void *const userdata);
int xmppintf_handle_iq_result(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata);
int xmppintf_handle_last_activity_request(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata);
void xmppintf_handle_ping_reply(
    struct xmpp_t *const xmpp,
    xmpp_stanza_t *const stanza,
    void *const userdata,
    enum xmpp_request_status_t status);
int xmppintf_handle_presence(
    xmpp_conn_t *const xmpp,
    xmpp_stanza_t *const stanza,
    void *const userdata);
int xmppintf_handle_time_request(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata);
int xmppintf_handle_version_request(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata);
void xmppintf_send_iq_with_callback(
    struct xmpp_t *xmpp,
    xmpp_stanza_t *const stanza,
    iq_response_callback_t on_response,
    int32_t timeout,
    void *userdata);
int xmppintf_send_ping(
    xmpp_conn_t *const conn,
    void *const userdata);

/* implementation */

void xmppintf_clear_iq_heap(
    struct xmpp_t *xmpp)
{
    pthread_mutex_lock(&xmpp->iq_heap_mutex);
    for (intptr_t i = 0;
         i < array_length(&xmpp->iq_heap.array);
         ++i)
    {
        struct iq_callback_t *cb = array_get(
            &xmpp->iq_heap.array, i);
        cb->on_response(
            xmpp,
            NULL,
            cb->userdata,
            REQUEST_STATUS_DISCONNECTED);
        iq_callback_free(cb);
    }
    array_clear(&xmpp->iq_heap.array);
    pthread_mutex_unlock(&xmpp->iq_heap_mutex);
}

void xmppintf_conn_state_change(xmpp_conn_t * const conn,
    const xmpp_conn_event_t status,
    const int error,
    xmpp_stream_error_t * const stream_error,
    void * const userdata)
{
    struct xmpp_t *const xmpp = userdata;

    switch (status)
    {
    case XMPP_CONN_CONNECT:
    {
        fprintf(stderr, "xmpp: connected\n");
        xmpp_handler_add(
            conn,
            &xmppintf_handle_version_request,
            "jabber:iq:version",
            "iq",
            NULL,
            userdata);

        xmpp_handler_add(
            conn,
            &xmppintf_handle_last_activity_request,
            "jabber:iq:last",
            "iq",
            NULL,
            userdata);

        xmpp_handler_add(
            conn,
            &xmppintf_handle_time_request,
            "urn:xmpp:time",
            "iq",
            NULL,
            userdata);

        xmpp_handler_add(
            conn,
            &xmppintf_handle_iq_error,
            NULL,
            "iq",
            "error",
            userdata);
        xmpp_handler_add(
            conn,
            &xmppintf_handle_iq_result,
            NULL,
            "iq",
            "result",
            userdata);

        xmpp_handler_add(
            conn,
            &xmppintf_handle_presence,
            NULL,
            "presence",
            NULL,
            userdata);

        xmpp_timed_handler_add(
            conn,
            &xmppintf_handle_iq_timeout,
            250,
            userdata);

        xmppintf_set_presence(xmpp, PRESENCE_AVAILABLE, NULL);
        send_char(xmpp->my_recv_fd, XMPPINTF_PIPECHAR_READY);
        xmppintf_send_ping(xmpp->conn, xmpp);
        break;
    }
    case XMPP_CONN_DISCONNECT:
    {
        fprintf(stderr, "xmpp: disconnected\n");
        send_char(xmpp->my_recv_fd, XMPPINTF_PIPECHAR_FAILED);
        xmpp_stop(xmpp->ctx);
        break;
    }
    case XMPP_CONN_FAIL:
    {
        fprintf(stderr, "xmpp: failed\n");
        send_char(xmpp->my_recv_fd, XMPPINTF_PIPECHAR_FAILED);
        xmpp_stop(xmpp->ctx);
        break;
    }
    }
}

void xmppintf_free(struct xmpp_t *xmpp)
{
    fprintf(stderr, "debug: xmppintf: free\n");
    pthread_mutex_lock(&xmpp->conn_mutex);
    xmpp->terminated = true;
    if (xmpp->conn) {
        xmpp_disconnect(xmpp->conn);
    }
    pthread_mutex_unlock(&xmpp->conn_mutex);
    pthread_join(xmpp->thread, NULL);

    if (xmpp->jid) {
        free(xmpp->jid);
    }
    if (xmpp->pass) {
        free(xmpp->pass);
    }
    free(xmpp->ping.peer);
    free(xmpp->weather.peer);
    free(xmpp->departure.peer);

    pthread_mutex_destroy(&xmpp->iq_heap_mutex);
    pthread_mutex_destroy(&xmpp->conn_mutex);
    pthread_mutex_destroy(&xmpp->serial_mutex);
    pthread_mutex_destroy(&xmpp->status_mutex);

    queue_free(&xmpp->recv_queue);
    heap_free(&xmpp->iq_heap);

    if (xmpp->ctx) {
        xmpp_ctx_free(xmpp->ctx);
    }

    xmpp_shutdown();
    fprintf(stderr, "debug: xmppintf: freed completely\n");
}

void xmppintf_free_queue_item(struct xmpp_queue_item_t *item)
{
    switch (item->type)
    {
    case XMPP_DEPARTURE_DATA:
    {
        array_free(&item->data.departure->entries);
        free(item->data.departure);
        break;
    }
    case XMPP_WEATHER_DATA:
    {
        break;
    }
    default:
    {
        panicf("xmppintf: error: unknown queue item type in free: %d\n",
               item->type);
    }
    }
    free(item);
}

char *xmppintf_get_next_id(struct xmpp_t *xmpp)
{
    static const int len = 32;
    pthread_mutex_lock(&xmpp->serial_mutex);
    int serial = ++xmpp->serial;
    pthread_mutex_unlock(&xmpp->serial_mutex);
    char *result = malloc(len);
    const int used = snprintf(
        result, len, "%d", serial);
    assert(used < len);
    return result;
}

static int xmppintf_handle_iq_reply(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata,
    enum xmpp_request_status_t status)
{
    struct xmpp_t *const xmpp = userdata;
    const char *id = xmpp_stanza_get_id(stanza);

    pthread_mutex_lock(&xmpp->iq_heap_mutex);
    for (intptr_t i = 0; i < array_length(&xmpp->iq_heap.array); i++)
    {
        struct iq_callback_t *cb = array_get(
            &xmpp->iq_heap.array, i);
        if (strcmp(cb->id, id) == 0) {
            heap_delete(&xmpp->iq_heap, i);
            cb->on_response(
                xmpp,
                stanza,
                cb->userdata,
                status);
            iq_callback_free(cb);
            break;
        }
    }
    pthread_mutex_unlock(&xmpp->iq_heap_mutex);

    return 1;
}

void xmppintf_handle_departure_reply(
    struct xmpp_t *const xmpp,
    xmpp_stanza_t *const stanza,
    void *const userdata,
    enum xmpp_request_status_t status)
{
    struct departure_callback_t *cb = userdata;
    switch (status)
    {
    case REQUEST_STATUS_ERROR:
    {
        xmpp_stanza_t *error_stanza = xmpp_stanza_get_children(stanza);
        if (error_stanza) {
            error_stanza = xmpp_stanza_get_children(error_stanza);
        }
        fprintf(stderr,
                "xmpp: departure_reply: error: %s\n",
                (error_stanza
                 ? xmpp_stanza_get_name(error_stanza) :
                 "no error stanza supplied"));
        // fallthrough to error forwarding
    }
    case REQUEST_STATUS_TIMEOUT:
    case REQUEST_STATUS_DISCONNECTED:
    {
        cb->callback(
            xmpp,
            NULL,
            cb->userdata,
            (stanza ? REQUEST_STATUS_ERROR : REQUEST_STATUS_TIMEOUT));
        departure_callback_free(cb);
        return;
    }
    case REQUEST_STATUS_SUCCESS:
    {
        break;
    }
    }

    struct array_t *result = malloc(sizeof(struct array_t));
    array_init(result, 4);

    xmpp_stanza_t *traversal = xmpp_stanza_get_children(stanza);
    if (!traversal) {
        goto traversal_error;
    }
    if (strcmp(
            xmpp_stanza_get_name(traversal),
            xml_pt_departure) != 0)
    {
        goto traversal_error;
    }

    traversal = xmpp_stanza_get_children(traversal);
    if (!traversal) {
        goto traversal_error;
    }
    if (strcmp(
            xmpp_stanza_get_name(traversal),
            xml_pt_data) != 0)
    {
        goto traversal_error;
    }


    for (xmpp_stanza_t *dt_stanza = xmpp_stanza_get_children(traversal);
         dt_stanza != NULL;
         dt_stanza = xmpp_stanza_get_next(dt_stanza))
    {
        if (xmpp_stanza_is_text(dt_stanza)) {
            continue;
        }
        if (strcmp(
                xmpp_stanza_get_name(dt_stanza),
                xml_pt_departure_time) != 0)
        {
            fprintf(stderr,
                    "xmpp: departure_reply: "
                    "unknown <data /> child: <%s />\n",
                    xmpp_stanza_get_name(dt_stanza));
            continue;
        }

        char *eta = xmpp_stanza_get_attribute(
            dt_stanza,
            xml_pt_attr_departure_time_eta);
        char *dest = xmpp_stanza_get_attribute(
            dt_stanza,
            xml_pt_attr_departure_time_destination);
        char *lane = xmpp_stanza_get_attribute(
            dt_stanza,
            xml_pt_attr_departure_time_lane);
        char *timestamp = xmpp_stanza_get_attribute(
            dt_stanza,
            xml_pt_attr_departure_time_timestamp);
        char *dir = xmpp_stanza_get_attribute(
            dt_stanza,
            xml_pt_attr_departure_time_dir);
        if (!eta || !dest || !lane) {
            fprintf(stderr,
                "xmpp: departure_reply: missing "
                "attributes on <dt />\n");
            goto error;
        }
        if (*eta == '\0') {
            fprintf(stderr,
                "xmpp: departure_reply: @eta is "
                "empty string\n");
            goto error;
        }
        if (strlen(lane) > DEPARTURE_LANE_LENGTH) {
            fprintf(stderr,
                "xmpp: departure_reply: @lane is "
                "too long: %s\n", lane);
        }

        struct dept_row_t *row = malloc(sizeof(struct dept_row_t));
        if (!row) {
            goto out_of_memory;
        }

        strncpy(row->lane, lane, DEPARTURE_LANE_LENGTH+1);
        row->lane[2] = '\0';
        char *endptr = NULL;
        row->eta = strtol(eta, &endptr, 10);
        if (*endptr != '\0') {
            // we checked *eta == '\0' before!
            fprintf(stderr,
                "xmpp: departure_reply: @eta is "
                "not integer\n");
            free(row);
            goto error;
        }
        row->destination = strdup(dest);
        if (!row->destination) {
            free(row);
            goto out_of_memory;
        }

        if (!timestamp || *timestamp == '\0') {
            row->timestamp = 0;
        } else {
            row->timestamp = strtoll(timestamp, &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr,
                        "xmpp: departure_reply: @ts is not integer "
                        "-- ignoring trailing data \n");
            }
        }

        if (!dir || *dir == '\0') {
            row->dir = DEPARTURE_DIR_UNKNOWN;
        } else {
            row->dir = strtoll(dir, &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr,
                        "xmpp: departure_reply: @dir is not integer "
                        "-- ignoring trailing data \n");
            }
        }

        array_append(result, row);
    }

    cb->callback(
        xmpp,
        result,
        cb->userdata,
        REQUEST_STATUS_SUCCESS);
    departure_callback_free(cb);
    return;

traversal_error:
    if (traversal) {
        fprintf(stderr,
                "xmpp: departure_reply: unexpected child: <%s />\n",
                xmpp_stanza_get_name(traversal));
    } else {
        fprintf(stderr,
                "xmpp: departure_reply: unexpected childless element\n");
    }
    goto error;

out_of_memory:
    fprintf(stderr, "xmpp: departure_reply: out of memory\n");
error:
    while (!array_empty(result)) {
        struct dept_row_t *row = array_pop(result, -1);
        free(row->destination);
        free(row);
    }
    cb->callback(
        xmpp,
        NULL,
        cb->userdata,
        REQUEST_STATUS_ERROR);
    departure_callback_free(cb);
}

int xmppintf_handle_iq_error(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata)
{
    return xmppintf_handle_iq_reply(
        conn,
        stanza,
        userdata,
        REQUEST_STATUS_ERROR);
}

int xmppintf_handle_iq_result(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata)
{
    return xmppintf_handle_iq_reply(
        conn,
        stanza,
        userdata,
        REQUEST_STATUS_SUCCESS);
}

int xmppintf_handle_iq_timeout(
    xmpp_conn_t *const conn,
    void *const userdata)
{
    struct xmpp_t *const xmpp = userdata;

    pthread_mutex_lock(&xmpp->iq_heap_mutex);
    while (heap_length(&xmpp->iq_heap) > 0) {
        struct iq_callback_t *cb = heap_get_min(&xmpp->iq_heap);
        struct timespec now;
        timestamp_gettime(&now);
        if (!timestamp_less(&cb->timeout_at, &now)) {
            break;
        }
        heap_pop_min(&xmpp->iq_heap);
        cb->on_response(
            xmpp,
            NULL,
            cb->userdata,
            REQUEST_STATUS_TIMEOUT);
        iq_callback_free(cb);
    }
    pthread_mutex_unlock(&xmpp->iq_heap_mutex);
    return 1;
}

int xmppintf_handle_last_activity_request(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata)
{
    struct xmpp_t *const state = userdata;
    xmpp_ctx_t *ctx = state->ctx;
    const char *from = xmpp_stanza_get_attribute(stanza, "from");

    xmpp_stanza_t *query;
    xmpp_stanza_t *reply = iq(ctx, "result", from, xmpp_stanza_get_id(stanza));

    query = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(query, "query");
    char *ns = xmpp_stanza_get_ns(xmpp_stanza_get_children(stanza));
    if (ns) {
        xmpp_stanza_set_ns(query, ns);
    }
    xmpp_stanza_set_attribute(query, "seconds", "0");

    xmpp_stanza_add_child(reply, query);
    xmpp_stanza_release(query);

    xmpp_send(conn, reply);
    xmpp_stanza_release(reply);
    return 1;
}

void xmppintf_handle_sensor_submission_reply(
    struct xmpp_t *const xmpp,
    xmpp_stanza_t *const stanza,
    void *const userdata,
    enum xmpp_request_status_t status)
{
    struct sensor_submission_callback_t *cb = userdata;

    switch (status)
    {
    case REQUEST_STATUS_ERROR:
    {
        xmpp_stanza_t *error_stanza = xmpp_stanza_get_children(stanza);
        if (error_stanza) {
            error_stanza = xmpp_stanza_get_children(error_stanza);
        }
        fprintf(stderr,
                "xmpp: sensor_submission_reply: error: %s\n",
                (error_stanza
                 ? xmpp_stanza_get_name(error_stanza) :
                 "no error stanza supplied"));
        break;
    }
    default:
    {
        break;
    }
    }

    cb->callback(
        xmpp,
        cb->batch,
        cb->userdata,
        status);

    sensor_submission_callback_free(cb);
}

void xmppintf_handle_weather_reply(
    struct xmpp_t *const xmpp,
    xmpp_stanza_t *const stanza,
    void *const userdata,
    enum xmpp_request_status_t status)
{
    struct weather_callback_t *cb = userdata;

    switch (status)
    {
    case REQUEST_STATUS_ERROR:
    {
        xmpp_stanza_t *error_stanza = xmpp_stanza_get_children(stanza);
        if (error_stanza) {
            error_stanza = xmpp_stanza_get_children(error_stanza);
        }
        fprintf(stderr,
                "xmpp: weather_reply: error: %s\n",
                (error_stanza
                 ? xmpp_stanza_get_name(error_stanza) :
                 "no error stanza supplied"));
        // fallthrough to error forwarding
    }
    case REQUEST_STATUS_TIMEOUT:
    case REQUEST_STATUS_DISCONNECTED:
    {
        cb->callback(
            xmpp,
            cb->data,
            cb->userdata,
            (stanza ? REQUEST_STATUS_ERROR : REQUEST_STATUS_TIMEOUT));
        weather_callback_free(cb);
        return;
    }
    case REQUEST_STATUS_SUCCESS:
    {
        break;
    }
    }

    struct array_t *result = cb->data;
    intptr_t i = 0;
    for (xmpp_stanza_t *interval_stanza = xmpp_stanza_get_children(
             xmpp_stanza_get_children(stanza));
         interval_stanza != NULL;
         interval_stanza = xmpp_stanza_get_next(interval_stanza))
    {
        if (xmpp_stanza_is_text(interval_stanza)) {
            continue;
        }
        if (strcmp(
                xmpp_stanza_get_name(interval_stanza),
                xml_meteo_interval) != 0) {
            continue;
        }

        struct weather_interval_t *dest = array_get(
            result, i);

        time_t tmp;
        if (!parse_isodate(
                xmpp_stanza_get_attribute(
                    interval_stanza,
                    xml_meteo_attr_interval_start),
                &tmp))
        {
            fprintf(stderr,
                    "xmpp: weather_reply: failed to parse isodate: %s\n",
                    xmpp_stanza_get_attribute(
                        interval_stanza,
                        xml_meteo_attr_interval_start));
            goto respond_with_error;
        }
        if (tmp != dest->start) {
            fprintf(stderr,
                    "xmpp: date mismatch: %ld != %ld\n",
                    tmp,
                    dest->start);
            goto respond_with_error;
        }
        dest->start = tmp;
        if (!parse_isodate(
                xmpp_stanza_get_attribute(
                    interval_stanza,
                    xml_meteo_attr_interval_end),
                &tmp))
        {
            fprintf(stderr,
                    "xmpp: weather_reply: failed to parse isodate: %s\n",
                    xmpp_stanza_get_attribute(
                        interval_stanza,
                        xml_meteo_attr_interval_end));
            goto respond_with_error;
        }
        if (tmp != dest->end) {
            fprintf(stderr,
                    "xmpp: date mismatch: %ld != %ld\n",
                    tmp,
                    dest->end);
            goto respond_with_error;
        }
        dest->end = tmp;

        dest->temperature_celsius = NAN;
        dest->precipitation_millimeter = NAN;
        dest->cloudiness_percent = NAN;
        dest->humidity_percent = NAN;
        dest->windspeed_meter_per_second = NAN;
        dest->precipitation_probability = NAN;

        for (xmpp_stanza_t *attr_stanza =
                 xmpp_stanza_get_children(interval_stanza);
             attr_stanza != NULL;
             attr_stanza = xmpp_stanza_get_next(attr_stanza))
        {
            if (xmpp_stanza_is_text(attr_stanza)) {
                continue;
            }
            const char *const attr_name =
                xmpp_stanza_get_name(attr_stanza);

            if (strcmp(attr_name, xml_meteo_temperature) == 0) {
                if (strcmp(
                        xmpp_stanza_get_attribute(
                            attr_stanza,
                            xml_meteo_attr_temperature_type),
                        xml_meteo_attr_temperature_type_air))
                {
                    fprintf(stderr,
                            "xmpp: weather_reply: unhandled temperature type: %s\n",
                            xmpp_stanza_get_attribute(
                                attr_stanza,
                                xml_meteo_attr_temperature_type));
                    continue;
                }
                float tmp;
                if (!parse_float(
                        xmpp_stanza_get_attribute(
                            attr_stanza,
                            xml_meteo_attr_value),
                        &tmp))
                {
                    fprintf(stderr,
                            "xmpp: weather_reply: failed to parse temperature\n");
                    goto respond_with_error;
                }
                dest->temperature_celsius = kelvin_to_celsius(tmp);

            } else if (strcmp(attr_name, xml_meteo_cloudcoverage) == 0) {
                if (strcmp(
                        xmpp_stanza_get_attribute(
                            attr_stanza,
                            xml_meteo_attr_cloudcoverage_level),
                        xml_meteo_attr_cloudcoverage_level_all))
                {
                    fprintf(stderr,
                            "xmpp: weather_reply: unhandled cloudiness level: %s\n",
                            xmpp_stanza_get_attribute(
                                attr_stanza,
                                xml_meteo_attr_cloudcoverage_level));
                    continue;
                }

                if (!parse_float(
                        xmpp_stanza_get_attribute(
                            attr_stanza,
                            xml_meteo_attr_value),
                        &dest->cloudiness_percent))
                {
                    fprintf(stderr,
                            "xmpp: weather_reply: failed to parse cloudiness\n");
                    goto respond_with_error;
                }

            } else if (strcmp(attr_name, xml_meteo_precipitation) == 0) {
                if (!parse_float(
                        xmpp_stanza_get_attribute(
                            attr_stanza,
                            xml_meteo_attr_value),
                        &dest->precipitation_millimeter))
                {
                    fprintf(stderr,
                            "xmpp: weather_reply: failed to parse precipitation\n");
                    goto respond_with_error;
                }

            } else if (strcmp(attr_name, xml_meteo_precipitation_probability) == 0) {
                if (!parse_float(
                        xmpp_stanza_get_attribute(
                            attr_stanza,
                            xml_meteo_attr_max),
                        &dest->precipitation_probability))
                {
                    fprintf(stderr,
                            "xmpp: weather_reply: failed to parse precipitation probability\n");
                    goto respond_with_error;
                }

            } else if (strcmp(attr_name, xml_meteo_wind_speed) == 0) {
                if (!parse_float(
                        xmpp_stanza_get_attribute(
                            attr_stanza,
                            xml_meteo_attr_value),
                        &dest->windspeed_meter_per_second))
                {
                    fprintf(stderr,
                            "xmpp: weather_reply: failed to parse wind speed\n");
                    goto respond_with_error;
                }

            } else if (strcmp(attr_name, xml_meteo_humidity) == 0) {
                if (!parse_float(
                        xmpp_stanza_get_attribute(
                            attr_stanza,
                            xml_meteo_attr_value),
                        &dest->humidity_percent))
                {
                    fprintf(stderr,
                            "xmpp: weather_reply: failed to parse humidity\n");
                    goto respond_with_error;
                }

            } else {
                fprintf(stderr,
                        "xmpp: weather_reply: unhandled attribute tag: %s\n",
                        attr_name);
                continue;
            }
        }

        ++i;
        if (i >= array_length(result)) {
            break;
        }
    }

    cb->callback(
        xmpp,
        cb->data,
        cb->userdata,
        REQUEST_STATUS_SUCCESS);

    weather_callback_free(cb);
    return;

respond_with_error:
    cb->callback(
        xmpp,
        cb->data,
        cb->userdata,
        REQUEST_STATUS_ERROR);
    weather_callback_free(cb);
    return;
}

void xmppintf_handle_ping_reply(
    struct xmpp_t *xmpp,
    xmpp_stanza_t *const stanza,
    void *const userdata,
    enum xmpp_request_status_t status)
{
    if (!xmpp->ping.pending) {
        return;
    }

    xmpp->ping.pending = false;

    switch (status)
    {
    case REQUEST_STATUS_DISCONNECTED:
    {
        // ignore
        return;
    }
    case REQUEST_STATUS_TIMEOUT:
    {
        fprintf(stderr, "xmpp: ping timeout\n");
        xmpp_disconnect(xmpp->conn);
        return;
    }
    case REQUEST_STATUS_ERROR:
    {
        fprintf(stderr, "xmpp: ping error\n");
        // fallthrough to success, we don’t care a lot about errors,
        // the point is, to receive an error, we must be connected
    }
    case REQUEST_STATUS_SUCCESS:
    {
        break;
    }
    }

    xmpp_timed_handler_add(
        xmpp->conn,
        xmppintf_send_ping,
        xmpp->ping.probe_interval,
        xmpp);
}

int xmppintf_handle_presence(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata)
{
    struct xmpp_t *const state = userdata;

    const char *from = xmpp_stanza_get_attribute(stanza, "from");
    const char *type = xmpp_stanza_get_attribute(stanza, "type");

    fprintf(stderr, "xmppintf: debug: presence from='%s' type=%s\n",
            from, (type == NULL ? "NULL" : type));

    bool available;
    if (type && (strcmp(type, "unavailable") == 0)) {
        available = false;
    } else if (!type) {
        available = true;
    } else {
        return 1;
    }

    if (strcmp(from, state->weather.peer) == 0) {
        pthread_mutex_lock(&state->status_mutex);
        state->weather.peer_available = available;
        pthread_mutex_unlock(&state->status_mutex);
    }
    if (strcmp(from, state->departure.peer) == 0) {
        pthread_mutex_lock(&state->status_mutex);
        state->departure.peer_available = available;
        pthread_mutex_unlock(&state->status_mutex);
    }

    return 1;
}

int xmppintf_handle_time_request(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata)
{
    struct xmpp_t *const state = userdata;
    xmpp_ctx_t *ctx = state->ctx;
    const char *from = xmpp_stanza_get_attribute(stanza, "from");

    xmpp_stanza_t *timestanza, *tzo, *utc;
    xmpp_stanza_t *reply = iq(ctx, "result", from, xmpp_stanza_get_id(stanza));

    timestanza = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(timestanza, "time");
    char *ns = xmpp_stanza_get_ns(xmpp_stanza_get_children(stanza));
    if (ns) {
        xmpp_stanza_set_ns(timestanza, ns);
    }

    tzo = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(tzo, "tzo");
    add_text(ctx, tzo, "+00:00");
    xmpp_stanza_add_child(timestanza, tzo);
    xmpp_stanza_release(tzo);

    utc = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(utc, "utc");
    {
        static isodate_buffer buffer;
        time_t timestamp = time(NULL);
        struct tm *utctime = gmtime(&timestamp);
        strftime(buffer, ISODATE_LENGTH+1, isodate_fmt, utctime);
        add_text(ctx, utc, buffer);
    }
    xmpp_stanza_add_child(timestanza, utc);
    xmpp_stanza_release(utc);

    xmpp_stanza_add_child(reply, timestanza);
    xmpp_stanza_release(timestanza);

    xmpp_send(conn, reply);
    xmpp_stanza_release(reply);
    return 1;
}

int xmppintf_handle_version_request(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata)
{
    struct xmpp_t *const state = userdata;
    xmpp_ctx_t *ctx = state->ctx;
    const char *from = xmpp_stanza_get_attribute(stanza, "from");

    xmpp_stanza_t *query, *name, *version, *text;
    xmpp_stanza_t *reply = iq(ctx, "result", from, xmpp_stanza_get_id(stanza));

    query = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(query, "query");
    char *ns = xmpp_stanza_get_ns(xmpp_stanza_get_children(stanza));
    if (ns) {
        xmpp_stanza_set_ns(query, ns);
    }

    name = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(name, "name");
    xmpp_stanza_add_child(query, name);
    xmpp_stanza_release(name);

    text = xmpp_stanza_new(ctx);
    xmpp_stanza_set_text(text, "Home INformation Terminal Daemon (hintd)");
    xmpp_stanza_add_child(name, text);
    xmpp_stanza_release(text);

    version = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(version, "version");
    xmpp_stanza_add_child(query, version);
    xmpp_stanza_release(version);

    text = xmpp_stanza_new(ctx);
    xmpp_stanza_set_text(text, "unspecified");
    xmpp_stanza_add_child(version, text);
    xmpp_stanza_release(text);

    xmpp_stanza_add_child(reply, query);
    xmpp_stanza_release(query);

    xmpp_send(conn, reply);
    xmpp_stanza_release(reply);
    return 1;
}

void xmppintf_init(
    struct xmpp_t *xmpp,
    const char *jid,
    const char *pass,
    const char *ping_peer,
    const char *weather_peer,
    const char *departure_peer)
{
    int fds[2];
    if (pipe(fds) != 0) {
        fprintf(stderr, "xmpp: failed to allocate pipe\n");
        return;
    }

    xmpp->recv_fd = fds[0];
    xmpp->my_recv_fd = fds[1];

    xmpp->jid = strdup(jid);
    xmpp->pass = strdup(pass);
    pthread_mutex_init(&xmpp->serial_mutex, NULL);
    xmpp->serial = 0;
    xmpp->ping.peer = strdup(ping_peer);
    xmpp->ping.pending = false;
    xmpp->ping.timeout_interval = 15000;
    xmpp->ping.probe_interval = 10000;
    xmpp->weather.peer = strdup(weather_peer);
    xmpp->weather.timeout_interval = 6000;
    xmpp->departure.peer = strdup(departure_peer);
    xmpp->departure.timeout_interval = 29000;
    xmpp->terminated = false;

    queue_init(&xmpp->recv_queue);

    xmpp_initialize();

    xmpp->log = xmpp_get_default_logger(XMPP_LEVEL_INFO);
    xmpp->ctx = NULL;
    xmpp->conn = NULL;

    pthread_mutex_init(&xmpp->status_mutex, NULL);
    xmpp->curr_status = PRESENCE_UNAVAILABLE;

    pthread_mutex_init(&xmpp->iq_heap_mutex, NULL);
    heap_init(&xmpp->iq_heap, 32, (heap_less_t)&iq_callback_less);

    pthread_mutex_init(&xmpp->conn_mutex, NULL);
    pthread_create(
        &xmpp->thread, NULL, (void*(*)(void*))&xmppintf_thread, xmpp);
}

struct xmpp_queue_item_t *xmppintf_new_queue_item(
    enum xmpp_queue_item_type_t type)
{
    struct xmpp_queue_item_t *result =
        malloc(sizeof(struct xmpp_queue_item_t));
    if (!result) {
        return result;
    }

    result->type = type;
    switch (type)
    {
    case XMPP_DEPARTURE_DATA:
    {
        result->data.departure =
            malloc(sizeof(struct xmpp_departure_data_t));
        if (!result->data.departure) {
            free(result);
            return NULL;
        }

        array_init(&result->data.departure->entries, 4);
        break;
    }
    case XMPP_WEATHER_DATA:
    {
        result->data.weather =
            malloc(sizeof(struct xmpp_weather_data_t));
        if (!result->data.weather) {
            free(result);
            return NULL;
        }
        break;
    }
    default:
    {
        panicf("xmppintf: error: unknown queue item type in new: %d\n",
               result->type);
    }
    }
    return result;
}

bool xmppintf_is_available(
    struct xmpp_t *xmpp)
{
    bool result;
    pthread_mutex_lock(&xmpp->status_mutex);
    result = xmpp->curr_status != PRESENCE_UNAVAILABLE;
    pthread_mutex_unlock(&xmpp->status_mutex);
    return result;
}

bool xmppintf_request_departure_data(
    struct xmpp_t *xmpp,
    departure_callback_t callback,
    void *const userdata)
{
    pthread_mutex_lock(&xmpp->conn_mutex);
    if (!xmpp->conn) {
        goto leave_with_error;
    }

    xmpp_ctx_t *ctx = xmpp->ctx;

    char *id_str = xmppintf_get_next_id(xmpp);
    xmpp_stanza_t *root = iq(
        xmpp->ctx,
        "get",
        xmpp->departure.peer,
        id_str);
    free(id_str);

    xmpp_stanza_t *departure = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(departure, xml_pt_departure);
    xmpp_stanza_set_ns(departure, xmppintf_ns_public_transport);
    xmpp_stanza_add_child(root, departure);
    xmpp_stanza_release(departure);

    xmppintf_send_iq_with_callback(
        xmpp,
        root,
        &xmppintf_handle_departure_reply,
        xmpp->departure.timeout_interval,
        departure_callback_new(
            callback,
            userdata));

    pthread_mutex_unlock(&xmpp->conn_mutex);
    return true;

leave_with_error:
    pthread_mutex_unlock(&xmpp->conn_mutex);
    return false;
}

static inline void add_request_tags(
    xmpp_ctx_t *const ctx,
    xmpp_stanza_t *const interval)
{
    xmpp_stanza_t *stanza = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(stanza, xml_meteo_temperature);
    xmpp_stanza_set_attribute(
        stanza,
        xml_meteo_attr_temperature_type,
        xml_meteo_attr_temperature_type_air);
    xmpp_stanza_add_child(interval, stanza);
    xmpp_stanza_release(stanza);

    stanza = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(stanza, xml_meteo_cloudcoverage);
    xmpp_stanza_set_attribute(
        stanza,
        xml_meteo_attr_cloudcoverage_level,
        xml_meteo_attr_cloudcoverage_level_all);
    xmpp_stanza_add_child(interval, stanza);
    xmpp_stanza_release(stanza);

    stanza = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(stanza, xml_meteo_precipitation);
    xmpp_stanza_add_child(interval, stanza);
    xmpp_stanza_release(stanza);

    stanza = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(stanza, xml_meteo_wind_speed);
    xmpp_stanza_add_child(interval, stanza);
    xmpp_stanza_release(stanza);

    stanza = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(stanza, xml_meteo_humidity);
    xmpp_stanza_add_child(interval, stanza);
    xmpp_stanza_release(stanza);

    stanza = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(stanza, xml_meteo_precipitation_probability);
    xmpp_stanza_add_child(interval, stanza);
    xmpp_stanza_release(stanza);

}

bool xmppintf_request_weather_data(
    struct xmpp_t *xmpp,
    const float lat,
    const float lon,
    weather_callback_t callback,
    struct array_t *request_intervals,
    void *const userdata)
{
    if (array_length(request_intervals) == 0) {
        return false;
    }

    pthread_mutex_lock(&xmpp->conn_mutex);
    if (!xmpp->conn) {
        goto leave_with_error;
    }

    static char geocoord_buffer[32];
    static isodate_buffer date_buffer;

    xmpp_ctx_t *ctx = xmpp->ctx;

    char *id_str = xmppintf_get_next_id(xmpp);
    xmpp_stanza_t *root = iq(
        xmpp->ctx,
        "get",
        xmpp->weather.peer,
        id_str);
    free(id_str);

    xmpp_stanza_t *data = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(data, "data");
    xmpp_stanza_set_ns(data, xmppintf_ns_meteo_service);
    xmpp_stanza_set_attribute(data, "from",
                              CONFIG_WEATHER_SERVICE_URI);

    xmpp_stanza_t *loc = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(loc, "l");
    snprintf(&geocoord_buffer[0], 32, "%.4f", lat);
    xmpp_stanza_set_attribute(loc, "lat", geocoord_buffer);
    snprintf(&geocoord_buffer[0], 32, "%.4f", lon);
    xmpp_stanza_set_attribute(loc, "lon", geocoord_buffer);
    xmpp_stanza_add_child(data, loc);
    xmpp_stanza_release(loc);

    for (intptr_t i = 0; i < array_length(request_intervals); i++)
    {
        struct weather_interval_t *interval_data =
            (struct weather_interval_t*)array_get(request_intervals, i);

        xmpp_stanza_t *interval = xmpp_stanza_new(ctx);
        xmpp_stanza_set_name(interval, xml_meteo_interval);

        format_isodate(date_buffer, gmtime(&interval_data->start));
        xmpp_stanza_set_attribute(
            interval,
            xml_meteo_attr_interval_start,
            date_buffer);

        format_isodate(date_buffer, gmtime(&interval_data->end));
        xmpp_stanza_set_attribute(
            interval,
            xml_meteo_attr_interval_end,
            date_buffer);

        add_request_tags(ctx, interval);

        xmpp_stanza_add_child(data, interval);
        xmpp_stanza_release(interval);
    }

    xmpp_stanza_add_child(root, data);
    xmpp_stanza_release(data);

    xmppintf_send_iq_with_callback(
        xmpp,
        root,
        &xmppintf_handle_weather_reply,
        xmpp->weather.timeout_interval,
        weather_callback_new(
            callback,
            request_intervals,
            userdata));

    pthread_mutex_unlock(&xmpp->conn_mutex);
    return true;

leave_with_error:
    pthread_mutex_unlock(&xmpp->conn_mutex);
    return false;
}

void xmppintf_send_iq_with_callback(
    struct xmpp_t *xmpp,
    xmpp_stanza_t *const stanza,
    iq_response_callback_t on_response,
    int32_t timeout,
    void *userdata)
{
    struct iq_callback_t *cb = iq_callback_new(
        xmpp_stanza_get_id(stanza),
        on_response,
        timeout,
        userdata);

    pthread_mutex_lock(&xmpp->iq_heap_mutex);
    heap_insert(&xmpp->iq_heap, cb);
    pthread_mutex_unlock(&xmpp->iq_heap_mutex);

    xmpp_send(xmpp->conn, stanza);
    xmpp_stanza_release(stanza);
}

int xmppintf_send_ping(xmpp_conn_t *const conn, void *const userdata)
{
    struct xmpp_t *const xmpp = userdata;
    assert(!xmpp->ping.pending);

    xmpp_ctx_t *ctx = xmpp->ctx;

    char *id = xmppintf_get_next_id(xmpp);
    xmpp_stanza_t *iq_stanza = iq(ctx,
        "get",
        xmpp->ping.peer,
        id);
    free(id);

    xmpp_stanza_t *ping = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(ping, "ping");
    xmpp_stanza_set_ns(ping, xmppintf_ns_ping);
    xmpp_stanza_add_child(iq_stanza, ping);
    xmpp_stanza_release(ping);

    xmpp->ping.pending = true;

    xmppintf_send_iq_with_callback(
        xmpp,
        iq_stanza,
        &xmppintf_handle_ping_reply,
        xmpp->ping.timeout_interval,
        NULL);

    return 0;
}

void *xmppintf_thread(struct xmpp_t *xmpp)
{
    pthread_mutex_lock(&xmpp->conn_mutex);
    xmpp->ctx = xmpp_ctx_new(NULL, xmpp->log);
    while (!xmpp->terminated) {
        xmpp->conn = xmpp_conn_new(xmpp->ctx);
        xmpp_conn_set_jid(xmpp->conn, xmpp->jid);
        xmpp_conn_set_pass(xmpp->conn, xmpp->pass);
        fprintf(stderr, "xmpp: not terminated, trying to connect...\n");
        if (xmpp_connect_client(
                xmpp->conn,
                NULL,
                0,
                &xmppintf_conn_state_change, xmpp) != 0)
        {
            fprintf(stderr, "xmpp: xmpp_connect_client failed, retrying later\n");
            xmpp_conn_release(xmpp->conn);
            pthread_mutex_unlock(&xmpp->conn_mutex);
            sleep(15);
            pthread_mutex_lock(&xmpp->conn_mutex);
            continue;
        }
        pthread_mutex_unlock(&xmpp->conn_mutex);
        xmpp_resume(xmpp->ctx);
        pthread_mutex_lock(&xmpp->conn_mutex);
        pthread_mutex_lock(&xmpp->status_mutex);
        xmppintf_clear_iq_heap(xmpp);
        xmpp_conn_release(xmpp->conn);
        xmpp->conn = NULL;
        xmpp->curr_status = PRESENCE_UNAVAILABLE;
        xmpp->weather.peer_available = false;
        xmpp->departure.peer_available = false;
        pthread_mutex_unlock(&xmpp->status_mutex);
    }

    xmpp_ctx_free(xmpp->ctx);
    xmpp->ctx = NULL;
    pthread_mutex_unlock(&xmpp->conn_mutex);

    return NULL;
}

void xmppintf_set_presence(
    struct xmpp_t *xmpp,
    enum xmpp_presence_status_t new_status,
    const char *message)
{
    xmpp_ctx_t *const ctx = xmpp->ctx;
    pthread_mutex_lock(&xmpp->conn_mutex);
    xmpp_conn_t *const conn = xmpp->conn;
    assert(conn);

    pthread_mutex_lock(&xmpp->status_mutex);
    if ((new_status == xmpp->curr_status) && (!message)) {
        pthread_mutex_unlock(&xmpp->status_mutex);
        pthread_mutex_unlock(&xmpp->conn_mutex);
        return;
    }

    if ((new_status == PRESENCE_UNAVAILABLE) &&
        (new_status == xmpp->curr_status))
    {
        pthread_mutex_unlock(&xmpp->status_mutex);
        pthread_mutex_unlock(&xmpp->conn_mutex);
        return;
    }

    xmpp->curr_status = new_status;

    xmpp_stanza_t *presence = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(presence, "presence");

    switch (new_status) {
    case PRESENCE_UNAVAILABLE:
    {
        xmpp_stanza_set_attribute(presence, "type", "unavailable");
        break;
    }
    case PRESENCE_AWAY:
    {
        xmpp_stanza_t *node = NULL;
        xmpp_stanza_t *text = NULL;

        if (message) {
            node = xmpp_stanza_new(ctx);
            text = xmpp_stanza_new(ctx);
            xmpp_stanza_set_name(node, "show");
            xmpp_stanza_set_text(text, "away");
            xmpp_stanza_add_child(node, text);
            xmpp_stanza_release(text);
            xmpp_stanza_add_child(presence, node);
            xmpp_stanza_release(node);
        }

        // continue to PRESENCE_AVAILABLE for setting of message
    }
    case PRESENCE_AVAILABLE:
    {
        xmpp_stanza_t *node = NULL;
        xmpp_stanza_t *text = NULL;

        if (message) {
            node = xmpp_stanza_new(ctx);
            text = xmpp_stanza_new(ctx);
            xmpp_stanza_set_name(node, "status");
            xmpp_stanza_set_text(text, message);
            xmpp_stanza_add_child(node, text);
            xmpp_stanza_release(text);
            xmpp_stanza_add_child(presence, node);
            xmpp_stanza_release(node);
        }
        break;
    }
    }

    xmpp_send(conn, presence);
    xmpp_stanza_release(presence);
    pthread_mutex_unlock(&xmpp->status_mutex);
    pthread_mutex_unlock(&xmpp->conn_mutex);
}

bool xmppintf_submit_sensor_data(
    struct xmpp_t *xmpp,
    struct sensor_readout_batch_t *batch,
    sensor_submission_callback_t callback,
    void *const userdata)
{
    xmpp_ctx_t *const ctx = xmpp->ctx;
    pthread_mutex_lock(&xmpp->conn_mutex);
    xmpp_conn_t *const conn = xmpp->conn;
    if (!conn) {
        goto leave_with_error;
    }

    isodate_buffer date_buffer;
    char sensorid_buffer[15];
    char value_buffer[15];

    char *id_str = xmppintf_get_next_id(xmpp);
    xmpp_stanza_t *root = iq(
        xmpp->ctx,
        "set",
        xmpp->weather.peer,
        id_str);
    free(id_str);

    xmpp_stanza_t *data = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(data, xml_sensor_data);
    xmpp_stanza_set_ns(data, xmppintf_ns_sensor);

    for (int i = 0; i < batch->write_offset; i++) {
        struct sensor_readout_t *curr = &batch->data[i];
        format_isodate(date_buffer, gmtime(&curr->readout_time));
        snprintf(sensorid_buffer,
                 sizeof(sensorid_buffer),
                 "%02x%02x%02x%02x%02x%02x%02x",
                 curr->sensor_id[0],
                 curr->sensor_id[1],
                 curr->sensor_id[2],
                 curr->sensor_id[3],
                 curr->sensor_id[4],
                 curr->sensor_id[5],
                 curr->sensor_id[6]);
        snprintf(value_buffer,
                 sizeof(value_buffer),
                 "%hd",
                 curr->raw_value);

        xmpp_stanza_t *p = xmpp_stanza_new(ctx);
        xmpp_stanza_set_name(p, xml_sensor_point);
        xmpp_stanza_set_attribute(
            p, xml_sensor_attr_sensortype, "T");
        xmpp_stanza_set_attribute(
            p, xml_sensor_attr_sensorid, sensorid_buffer);
        xmpp_stanza_set_attribute(
            p, xml_sensor_attr_readout_time, date_buffer);
        xmpp_stanza_set_attribute(
            p, xml_sensor_attr_raw_value, value_buffer);
        xmpp_stanza_add_child(data, p);
        xmpp_stanza_release(p);
    }

    xmpp_stanza_add_child(root, data);
    xmpp_stanza_release(data);

    xmppintf_send_iq_with_callback(
        xmpp,
        root,
        &xmppintf_handle_sensor_submission_reply,
        xmpp->weather.timeout_interval,
        sensor_submission_callback_new(
            callback,
            batch,
            userdata));

    pthread_mutex_unlock(&xmpp->conn_mutex);
    return true;

leave_with_error:
    pthread_mutex_unlock(&xmpp->conn_mutex);
    return false;
}

bool xmppintf_weather_peer_is_available(
    struct xmpp_t *xmpp)
{
    bool result = false;
    pthread_mutex_lock(&xmpp->status_mutex);
    result = xmpp->weather.peer_available;
    pthread_mutex_unlock(&xmpp->status_mutex);
    return result;
}
