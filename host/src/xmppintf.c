#include "xmppintf.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

#include "utils.h"

const char *xmppintf_ns_sensor = "http://xmpp.zombofant.net/xmlns/sensor";

const char *xmppintf_ns_public_transport = "http://xmpp.zombofant.net/xmlns/public-transport";
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

int xmppintf_handle_last_activity_request(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata);
int xmppintf_handle_ping_reply(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata);
int xmppintf_handle_ping_timeout(
    xmpp_conn_t *const conn,
    void *const userdata);
int xmppintf_handle_public_transport_set(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata);
int xmppintf_handle_public_transport_set_data(
    struct xmpp_t *xmpp,
    xmpp_stanza_t *const data_stanza);
int xmppintf_handle_time_request(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata);
int xmppintf_handle_version_request(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata);
int xmppintf_send_ping(
    xmpp_conn_t *const conn,
    void *const userdata);

/* implementation */

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
            &xmppintf_handle_public_transport_set,
            xmppintf_ns_public_transport,
            "iq",
            "set",
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
    default:
    {
        panicf("xmppintf: error: unknown queue item type in free: %d\n",
               item->type);
    }
    }
    free(item);
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

int xmppintf_handle_ping_reply(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata)
{
    struct xmpp_t *xmpp = userdata;
    if (!xmpp->ping.pending) {
        return 0;
    }

    xmpp->ping.pending = 0;
    xmpp_timed_handler_delete(
        conn,
        xmppintf_handle_ping_timeout);
    xmpp_timed_handler_add(
        conn,
        xmppintf_send_ping,
        xmpp->ping.probe_interval,
        userdata);
    return 0;
}

int xmppintf_handle_ping_timeout(
    xmpp_conn_t *const conn,
    void *const userdata)
{
    struct xmpp_t *xmpp = userdata;
    if (!xmpp->ping.pending) {
        return 0;
    }

    fprintf(stderr, "xmpp: ping timeout\n");
    xmpp_disconnect(xmpp->conn);
    xmpp->ping.pending = false;

    return 0;
}

int xmppintf_handle_public_transport_set(
    xmpp_conn_t *const conn,
    xmpp_stanza_t *const stanza,
    void *const userdata)
{
    struct xmpp_t *xmpp = userdata;

    assert(strcmp(xmpp_stanza_get_type(stanza), "set") == 0);
    assert(strcmp(xmpp_stanza_get_ns(stanza), xmppintf_ns_public_transport) == 0);

    xmpp_stanza_t *child = xmpp_stanza_get_children(stanza);
    if (!child) {
        xmpp_stanza_t *result = iq_error(
            xmpp->ctx,
            stanza,
            "modify",
            "bad-request",
            NULL);
        xmpp_send(conn, result);
        xmpp_stanza_release(result);
        return 1;
    }

    if ((strcmp(xmpp_stanza_get_ns(child),
                xmppintf_ns_public_transport) != 0) ||
        (strcmp(xmpp_stanza_get_name(child), "departure") != 0))
    {
        xmpp_stanza_t *result = iq_error(
            xmpp->ctx,
            stanza,
            "modify",
            "feature-not-implemented",
            NULL);
        xmpp_send(conn, result);
        xmpp_stanza_release(result);
        return 1;
    }

    child = xmpp_stanza_get_children(child);

    const char *child_name = xmpp_stanza_get_name(child);

    if (strcmp(child_name, "data") == 0) {
        if (xmppintf_handle_public_transport_set_data(xmpp, child)) {
            iq_reply_empty_result(conn, stanza);
        } else {
            xmpp_stanza_t *result = iq_error(
                xmpp->ctx,
                stanza,
                "modify",
                "bad-request",
                NULL);
            xmpp_send(conn, result);
            xmpp_stanza_release(result);
        }
    } else {
        xmpp_stanza_t *result = iq_error(
            xmpp->ctx,
            stanza,
            "modify",
            "feature-not-implemented",
            NULL);
        xmpp_send(conn, result);
        xmpp_stanza_release(result);
    }

    return 1;
}

int xmppintf_handle_public_transport_set_data(
    struct xmpp_t *xmpp,
    xmpp_stanza_t *const data_stanza)
{
    struct xmpp_queue_item_t *item = xmppintf_new_queue_item(
        XMPP_DEPARTURE_DATA);

    struct array_t *dept_array = &item->data.departure->entries;

    xmpp_stanza_t *child = xmpp_stanza_get_children(data_stanza);
    while (child) {
        char *eta = xmpp_stanza_get_attribute(child, "eta");
        char *dest = xmpp_stanza_get_attribute(child, "destination");
        char *lane = xmpp_stanza_get_attribute(child, "lane");
        if (!eta || !dest || !lane) {
            goto error;
        }
        if (*eta == '\0') {
            goto error;
        }
        if (strlen(lane) > 2) {
            goto error;
        }

        struct dept_row_t *row = malloc(sizeof(struct dept_row_t));
        if (!row) {
            goto out_of_memory;
        }

        // strcpy is safe here, we checked the length before
        strcpy(row->lane, lane);
        char *endptr = NULL;
        row->remaining_time = strtol(eta, &endptr, 10);
        if (*endptr == '\0') {
            // we checked *eta == '\0' before!
            free(row);
            goto error;
        }
        row->destination = strdup(dest);
        if (!row->destination) {
            free(row);
            goto out_of_memory;
        }
        array_push(dept_array, INTPTR_MAX, row);

        child = xmpp_stanza_get_next(child);
    }

    queue_push(&xmpp->recv_queue, item);
    send_char(xmpp->my_recv_fd, XMPPINTF_PIPECHAR_MESSAGE);

    return 1;

out_of_memory:
    fprintf(stderr, "xmppintf: out of memory, dropping data stanza\n");
error:
    while (!array_empty(dept_array)) {
        struct dept_row_t *row = array_pop(dept_array, -1);
        free(row->destination);
        free(row);
    }
    xmppintf_free_queue_item(item);
    return 0;
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
        static char buffer[21];
        time_t timestamp = time(NULL);
        struct tm *utctime = gmtime(&timestamp);
        strftime(buffer, 21, "%FT%H:%M:%SZ", utctime);
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
    const char *ping_peer)
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
    xmpp->ping.peer = strdup(ping_peer);
    xmpp->ping.serial = 0;
    xmpp->ping.pending = false;
    xmpp->ping.timeout_interval = 2000;
    xmpp->ping.probe_interval = 1000;
    xmpp->terminated = false;

    queue_init(&xmpp->recv_queue);

    xmpp_initialize();

    xmpp->log = xmpp_get_default_logger(XMPP_LEVEL_DEBUG);
    xmpp->ctx = NULL;
    xmpp->conn = NULL;
    xmpp->curr_status = PRESENCE_UNAVAILABLE;

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
    default:
    {
        panicf("xmppintf: error: unknown queue item type in new: %d\n",
               result->type);
    }
    }
    return result;
}

int xmppintf_send_ping(xmpp_conn_t *const conn, void *const userdata)
{
    struct xmpp_t *const xmpp = userdata;
    assert(!xmpp->ping.pending);

    static char pingidbuf[127];

    xmpp_ctx_t *ctx = xmpp->ctx;

    memset(pingidbuf, 0, 127);
    sprintf(pingidbuf, "ping%d", xmpp->ping.serial++);

    xmpp_stanza_t *iq_stanza = iq(ctx,
        "get",
        xmpp->ping.peer,
        pingidbuf
    );
    xmpp_stanza_t *ping = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(ping, "ping");
    xmpp_stanza_set_ns(ping, xmppintf_ns_ping);
    xmpp_stanza_add_child(iq_stanza, ping);
    xmpp_stanza_release(ping);

    xmpp->ping.pending = true;
    xmpp_id_handler_add(
        conn,
        xmppintf_handle_ping_reply,
        pingidbuf,
        userdata);
    xmpp_timed_handler_add(
        conn,
        xmppintf_handle_ping_timeout,
        xmpp->ping.timeout_interval,
        userdata);

    xmpp_send(conn, iq_stanza);
    xmpp_stanza_release(iq_stanza);

    return 0;
}

void *xmppintf_thread(struct xmpp_t *xmpp)
{
    pthread_mutex_lock(&xmpp->conn_mutex);
    while (!xmpp->terminated) {
        xmpp->ctx = xmpp_ctx_new(NULL, xmpp->log);
        xmpp->conn = xmpp_conn_new(xmpp->ctx);
        xmpp_conn_set_jid(xmpp->conn, xmpp->jid);
        xmpp_conn_set_pass(xmpp->conn, xmpp->pass);
        fprintf(stderr, "xmpp: not terminated, trying to connect...\n");
        if (xmpp_connect_client(xmpp->conn, NULL, 0, &xmppintf_conn_state_change, xmpp) != 0)
        {
            fprintf(stderr, "xmpp: xmpp_connect_client failed, retrying later\n");
            pthread_mutex_unlock(&xmpp->conn_mutex);
            sleep(3);
            pthread_mutex_lock(&xmpp->conn_mutex);
            continue;
        }
        pthread_mutex_unlock(&xmpp->conn_mutex);
        xmpp_resume(xmpp->ctx);
        pthread_mutex_lock(&xmpp->conn_mutex);
        xmpp_conn_release(xmpp->conn);
        xmpp->conn = NULL;
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
    xmpp_conn_t *const conn = xmpp->conn;
    assert(conn);

    if ((new_status == xmpp->curr_status) && (!message)) {
        return;
    }

    if ((new_status == PRESENCE_UNAVAILABLE) &&
        (new_status == xmpp->curr_status))
    {
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
}
