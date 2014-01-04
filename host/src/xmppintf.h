#ifndef _XMPP_H
#define _XMPP_H

#include <pthread.h>
#include <libcouplet/couplet.h>

#include "queue.h"
#include "array.h"
#include "departure.h"
#include "heap.h"

#define XMPPINTF_PIPECHAR_READY ('r')
#define XMPPINTF_PIPECHAR_FAILED ('f')
#define XMPPINTF_PIPECHAR_MESSAGE ('m')

extern const char *xmppintf_ns_sensor;
extern const char *xmppintf_ns_public_transport;
extern const char *xmppintf_ns_ping;

enum xmpp_presence_status_t {
    PRESENCE_AVAILABLE,
    PRESENCE_AWAY,
    PRESENCE_UNAVAILABLE
};

enum xmpp_queue_item_type_t {
    XMPP_DEPARTURE_DATA,
    XMPP_WEATHER_DATA
};

enum xmpp_request_status_t {
    REQUEST_STATUS_TIMEOUT,
    REQUEST_STATUS_ERROR,
    REQUEST_STATUS_SUCCESS,
    REQUEST_STATUS_DISCONNECTED
};

struct xmpp_departure_data_t {
    struct array_t entries;
};

struct xmpp_weather_data_t {
    enum xmpp_request_status_t status;
};

struct xmpp_queue_item_t {
    enum xmpp_queue_item_type_t type;
    union {
        struct xmpp_departure_data_t *departure;
        struct xmpp_weather_data_t *weather;
    } data;
};

struct xmpp_t {
    xmpp_log_t *log;
    xmpp_ctx_t *ctx;
    xmpp_conn_t *conn;

    pthread_t thread;
    pthread_mutex_t conn_mutex;
    char *jid, *pass;
    bool terminated;

    int recv_fd;
    struct queue_t recv_queue;

    int my_recv_fd;
    enum xmpp_presence_status_t curr_status;

    int serial;

    pthread_mutex_t iq_heap_mutex;
    struct heap_t iq_heap;

    struct {
        bool pending;
        char *peer;
        int timeout_interval;
        int probe_interval;
    } ping;
    struct {
        char *peer;
        int timeout_interval;
    } weather;
    struct {
        char *peer;
        int timeout_interval;
    } departure;
};

typedef void (*weather_callback_t)(
    struct xmpp_t *xmpp,
    struct array_t *arr,
    void *const userdata,
    enum xmpp_request_status_t status);
typedef void (*departure_callback_t)(
    struct xmpp_t *xmpp,
    struct array_t *arr,
    void *const userdata,
    enum xmpp_request_status_t status);

void xmppintf_free(struct xmpp_t *xmpp);
void xmppintf_free_queue_item(struct xmpp_queue_item_t *item);
void xmppintf_init(
    struct xmpp_t *xmpp,
    const char *jid,
    const char *pass,
    const char *ping_peer,
    const char *weather_peer,
    const char *departure_peer);
bool xmppintf_is_available(
    struct xmpp_t *xmpp);
struct xmpp_queue_item_t *xmppintf_new_queue_item(
    enum xmpp_queue_item_type_t type);
void *xmppintf_thread(struct xmpp_t *xmpp);
void xmppintf_set_presence(
    struct xmpp_t *xmpp,
    enum xmpp_presence_status_t new_status,
    const char *message);
bool xmppintf_request_departure_data(
    struct xmpp_t *xmpp,
    departure_callback_t callback,
    void *const userdata);
bool xmppintf_request_weather_data(
    struct xmpp_t *xmpp,
    const float lat,
    const float lon,
    weather_callback_t callback,
    struct array_t *request_intervals,
    void *const userdata);

#endif
