#ifndef _XMPP_H
#define _XMPP_H

#include <pthread.h>
#include <libcouplet/couplet.h>

#include "queue.h"
#include "array.h"
#include "departure.h"

extern const char *xmppintf_ns_sensor;
extern const char *xmppintf_ns_public_transport;
extern const char *xmppintf_ns_ping;

enum xmpp_presence_status_t {
    PRESENCE_AVAILABLE,
    PRESENCE_AWAY,
    PRESENCE_UNAVAILABLE
};

enum xmpp_queue_item_type_t {
    QUEUE_DEPARTURE_DATA
};

struct xmpp_departure_data_t {
    struct array_t entries;
};

struct xmpp_queue_item_t {
    enum xmpp_queue_item_type_t type;
    union {
        struct xmpp_departure_data_t *departure;
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

    struct {
        int serial;
        bool pending;
        char *peer;
        int timeout_interval;
        int probe_interval;
    } ping;
};

void xmppintf_free(struct xmpp_t *xmpp);
void xmppintf_free_queue_item(struct xmpp_queue_item_t *item);
void xmppintf_init(
    struct xmpp_t *xmpp,
    const char *jid,
    const char *pass,
    const char *ping_peer);
struct xmpp_queue_item_t *xmppintf_new_queue_item(
    enum xmpp_queue_item_type_t type);
void *xmppintf_thread(struct xmpp_t *xmpp);
void xmppintf_set_presence(
    struct xmpp_t *xmpp,
    enum xmpp_presence_status_t new_status,
    const char *message);

#endif
