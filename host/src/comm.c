#include "comm.h"

#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>

#include "utils.h"
#include "timestamp.h"

void dump_buffer(FILE *dest, const uint8_t *buffer, int len)
{
    if (len == 0) {
        return;
    }
    for (int i = 0; i < len; i++) {
        const char *space = " ";
        if (i == 0) {
            space = "";
        } else if (i % 25 == 0) {
            space = "\n";
        }
        fprintf(dest, "%s%02x", space, *buffer++);
    }
    if (len > 0) {
        fprintf(dest, "\n");
    }
}

speed_t get_baudrate(uint32_t baudrate)
{
    switch (baudrate)
    {
    case 50:
        return B50;
    case 75:
        return B75;
    case 110:
        return B110;
    case 134:
        return B134;
    case 150:
        return B150;
    case 200:
        return B200;
    case 300:
        return B300;
    case 600:
        return B600;
    case 1200:
        return B1200;
    case 1800:
        return B1800;
    case 2400:
        return B2400;
    case 4800:
        return B4800;
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    case 230400:
        return B230400;
    case 460800:
        return B460800;
    case 500000:
        return B500000;
    case 576000:
        return B576000;
    case 921600:
        return B921600;
    case 1000000:
        return B1000000;
    case 1152000:
        return B1152000;
    case 1500000:
        return B1500000;
    case 2000000:
        return B2000000;
    case 2500000:
        return B2500000;
    case 3000000:
        return B3000000;
    case 3500000:
        return B3500000;
    case 4000000:
        return B4000000;
    default:
    {
        fprintf(stderr, "invalid baud rate: %d\n", baudrate);
        return B0;
    }
    }

}

// forward declarations

void *comm_alloc_message(
    const msg_address_t recipient,
    const msg_length_t payload_length);
bool comm_check_datafd_error(
    struct comm_t *comm,
    struct pollfd *datafd);
const char *comm_conn_state_str(const enum comm_conn_state_t state);
void comm_dump_body(
    const struct msg_header_t *hdr,
    const uint8_t *buffer);
void comm_dump_header(const struct msg_header_t *hdr);
void comm_dump_message(const struct msg_header_t *item);
void comm_enqueue_msg(struct comm_t *comm, void *msg);
void comm_init(
    struct comm_t *comm,
    const char *devfile,
    uint32_t baudrate);
bool comm_is_available(struct comm_t *comm);
int comm_open(struct comm_t *state);
enum comm_status_t comm_read_checked(
    int fd, void *const buf, intptr_t len);
enum comm_status_t comm_recv(
    int fd, struct msg_header_t *hdr, uint8_t **payload);
enum comm_status_t comm_send(
    int fd, const struct msg_header_t *hdr, const uint8_t *payload);
enum comm_status_t comm_send_ping(struct comm_t *comm);
void comm_timed_disable(struct comm_t *comm);
void comm_timed_in_future(struct comm_t *comm, int32_t msec);
char comm_wait(struct pollfd *pollfds, int timeout);
enum comm_status_t comm_write_checked(
    int fd, const void *const buf, intptr_t len);
void comm_thread_handle_packet(
    struct comm_t *state,
    struct msg_header_t *hdr,
    uint8_t *payload);
void comm_thread_handle_unexpected_control(
    struct comm_t *state,
    struct msg_header_t *hdr);
void comm_thread_signalfd(struct comm_t *comm, struct pollfd *signalfd);
bool comm_thread_state_open_tx(struct comm_t *state, uint8_t *buffer);
void comm_thread_state_closed(
    struct comm_t *comm,
    struct pollfd *datafd,
    bool timed_out);
void comm_thread_state_open(
    struct comm_t *comm,
    struct pollfd *datafd,
    bool timed_out);
void comm_thread_state_established(
    struct comm_t *comm,
    struct pollfd *datafd,
    bool timed_out);
void comm_thread_state_out_of_sync(
    struct comm_t *comm,
    struct pollfd *datafd,
    bool timed_out);
void comm_thread_to_state_closed(struct comm_t *comm);
void comm_thread_to_state_open(struct comm_t *comm);
void comm_thread_to_state_established(struct comm_t *comm);
void comm_thread_to_state_out_of_sync(struct comm_t *comm);
void *comm_thread(struct comm_t *comm);

// end of forward declarations

void *comm_alloc_message(
    const msg_address_t recipient,
    const msg_length_t payload_length)
{
    struct msg_header_t *msg = malloc(
        sizeof(struct msg_header_t) + payload_length);
    memset(msg, 0, sizeof(struct msg_header_t));
    HDR_SET_SENDER(*msg, MSG_ADDRESS_HOST);
    HDR_SET_RECIPIENT(*msg, recipient);
    HDR_SET_PAYLOAD_LENGTH(*msg, payload_length);
    return msg;
}

bool comm_check_datafd_error(
    struct comm_t *comm,
    struct pollfd *datafd)
{
    if (datafd->revents & (POLLERR|POLLHUP)) {
        fprintf(stderr, "comm: datafd error, switching to closed state\n");
        comm_thread_to_state_closed(comm);
        comm_timed_in_future(comm, 0);
        return true;
    }
    return false;
}

const char *comm_conn_state_str(const enum comm_conn_state_t state)
{
    switch  (state)
    {
    case COMM_CONN_CLOSED:
    {
        return "closed";
    }
    case COMM_CONN_OPEN:
    {
        return "open";
    }
    case COMM_CONN_ESTABLISHED:
    {
        return "established";
    }
    case COMM_CONN_OUT_OF_SYNC:
    {
        return "out-of-sync";
    }
    default:
    {
        return "??";
    }
    }
}

void comm_printf(const struct comm_t *comm, const char *format, ...)
{
    fprintf(stderr, "comm[%s]: ", comm_conn_state_str(comm->_conn_state));
    va_list arglist;
    va_start(arglist, format);
    vfprintf(stderr, format, arglist);
    va_end(arglist);
}

void comm_dump_body(const struct msg_header_t *hdr, const uint8_t *buffer)
{
    fprintf(stderr, "        message@%p: payload: \n", (void*)hdr);
    dump_buffer(stderr, buffer, HDR_GET_PAYLOAD_LENGTH(*hdr));
}

void comm_dump_checksum(const struct msg_header_t *hdr, const msg_checksum_t checksum)
{
    fprintf(stderr, "        message@%p: checksum: %02x\n", (void*)hdr, checksum);
}

void comm_dump_header(const struct msg_header_t *hdr)
{
    fprintf(stderr, "dumping message@%p: header: \n", (void*)hdr);
    dump_buffer(stderr, (const uint8_t*)hdr, sizeof(struct msg_header_t));
    fprintf(stderr, "    payload_length = 0x%02x\n",
                    HDR_GET_PAYLOAD_LENGTH(*hdr));
    fprintf(stderr, "    flags          = 0x%02x\n",
                    HDR_GET_FLAGS(*hdr));
    fprintf(stderr, "    sender         = 0x%01x\n",
                    HDR_GET_SENDER(*hdr));
    fprintf(stderr, "    recipient      = 0x%01x\n",
                    HDR_GET_RECIPIENT(*hdr));
}

void comm_dump_message(const struct msg_header_t *item)
{
    comm_dump_header(item);
    comm_dump_body(item, &((const uint8_t*)item)[sizeof(struct msg_header_t)]);
}

void comm_enqueue_msg(struct comm_t *comm, void *msg)
{
    queue_push(&comm->send_queue, msg);
    write(comm->signal_fd, "m", 1);
}

void comm_free(struct comm_t *comm)
{
    fprintf(stderr, "debug: comm: free\n");
    comm->terminated = true;
    write(comm->signal_fd, "w", 1);
    pthread_join(comm->thread, NULL);

    while (!queue_empty(&comm->send_queue))
    {
        free(queue_pop(&comm->send_queue));
    }
    queue_free(&comm->send_queue);

    while (!queue_empty(&comm->recv_queue))
    {
        free(queue_pop(&comm->recv_queue));
    }
    queue_free(&comm->recv_queue);

    close(comm->signal_fd);
    close(comm->_signal_fd);
    close(comm->recv_fd);
    close(comm->_recv_fd);

    if (comm->_pending_ack) {
        free(comm->_pending_ack);
    }

    pthread_mutex_destroy(&comm->data_mutex);
    free(comm->_devfile);
    fprintf(stderr, "debug: comm: freed completely\n");
}

void comm_init(
    struct comm_t *comm,
    const char *devfile,
    uint32_t baudrate)
{
    int fds[2];
    int status = pipe(fds);
    if (status != 0) {
        fprintf(stderr, "comm: failed to allocate pipe\n");
        return;
    }
    comm->_signal_fd = fds[0];
    comm->signal_fd = fds[1];

    status = pipe(fds);
    if (status != 0) {
        fprintf(stderr, "comm: failed to allocate pipe\n");
        return;
    }
    comm->_recv_fd = fds[1];
    comm->recv_fd = fds[0];

    comm->_devfile = strdup(devfile);
    comm->_fd = -1;
    comm->_baudrate = baudrate;
    comm->_pending_ack = NULL;
    comm->_conn_state = COMM_CONN_CLOSED;

    comm->terminated = false;

    queue_init(&comm->send_queue);
    queue_init(&comm->recv_queue);

    pthread_mutex_init(&comm->data_mutex, NULL);
    pthread_create(&comm->thread, NULL, (void*(*)(void*))&comm_thread, comm);
}

bool comm_is_available(struct comm_t *comm)
{
    bool result = true;
    pthread_mutex_lock(&comm->data_mutex);
    result = comm->_conn_state == COMM_CONN_ESTABLISHED;
    pthread_mutex_unlock(&comm->data_mutex);
    return result;
}

int comm_open(struct comm_t *state)
{
    int fd = open(state->_devfile, O_RDWR|O_CLOEXEC|O_NOCTTY);
    if (fd < 0) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        comm_printf(state, "fcntl(comm_device, F_SETFL, O_NONBLOCK) "
                           "failed: %d: %s\n",
                           errno,
                           strerror(errno));
    }

    struct termios port_settings;
    memset(&port_settings, 0, sizeof(port_settings));
    speed_t speed = get_baudrate(state->_baudrate);
    cfsetispeed(&port_settings, speed);
    cfsetospeed(&port_settings, speed);
    cfmakeraw(&port_settings);
    tcsetattr(fd, TCSANOW, &port_settings);
    tcflush(fd, TCIOFLUSH);

    pthread_mutex_lock(&state->data_mutex);
    state->_fd = fd;
    pthread_mutex_unlock(&state->data_mutex);
    return 0;
}

enum comm_status_t comm_read_checked(int fd, void *const buf, intptr_t len)
{
    struct pollfd pollfd;
    pollfd.fd = fd;
    pollfd.events = POLLIN;
    pollfd.revents = 0;

    intptr_t read_total = 0;
    uint8_t *buffer = buf;
    while (read_total < len) {
        int status = poll(&pollfd, 1, COMM_READ_TIMEOUT);
        if (status == 0) {
            fprintf(stderr, "comm: timeout: dumping buffer:\n");
            //~ dump_buffer(stderr, buf, read_total);
            return COMM_ERR_TIMEOUT;
        }
        if (pollfd.revents & (POLLERR|POLLHUP)) {
            return COMM_ERR_DISCONNECTED;
        } else if (pollfd.revents & POLLIN) {
            intptr_t read_this_time = read(fd, buffer, len - read_total);
            //~ fprintf(stdout, "<< ");
            //~ dump_buffer(stdout, buffer, read_this_time);
            assert(read_this_time > 0);
            read_total += read_this_time;
            buffer += read_this_time;
        }
    }
    return COMM_ERR_NONE;
}

enum comm_status_t comm_recv(int fd, struct msg_header_t *hdr, uint8_t **payload)
{
    struct msg_encoded_header_t encoded;
    enum comm_status_t result = comm_read_checked(
        fd, &encoded, sizeof(struct msg_encoded_header_t));
    if (result != COMM_ERR_NONE) {
        return result;
    }

    *hdr = wire_to_raw(&encoded);

    //~ fprintf(stderr, "== received ==\n");
    //~ comm_dump_header(hdr);

    const msg_length_t length = HDR_GET_PAYLOAD_LENGTH(*hdr);

    if (length > MSG_MAX_PAYLOAD) {
        return COMM_ERR_PROTOCOL_VIOLATION;
    }

    if (length == 0) {
        *payload = NULL;
        return COMM_ERR_CONTROL;
    }

    *payload = malloc(length);

    result = comm_read_checked(fd, *payload, length);
    if (result != COMM_ERR_NONE) {
        free(*payload);
        *payload = NULL;
        return result;
    }

    //~ comm_dump_body(hdr, *payload);

    msg_checksum_t received_checksum = 0;
    result = comm_read_checked(fd, &received_checksum, sizeof(msg_checksum_t));
    if (result != COMM_ERR_NONE) {
        free(*payload);
        *payload = NULL;
        return result;
    }

    //~ comm_dump_checksum(hdr, received_checksum);

    msg_checksum_t reference_checksum = checksum(*payload, length);
    if (received_checksum != reference_checksum) {
        comm_dump_header(hdr);
        comm_dump_body(hdr, *payload);
        comm_dump_checksum(hdr, received_checksum);
        comm_dump_checksum(hdr, reference_checksum);
        free(*payload);
        *payload = NULL;
        return COMM_ERR_CHECKSUM_ERROR;
    }

    if (HDR_GET_FLAGS(*hdr) != 0) {
        return COMM_ERR_FLAGS;
    }

    return COMM_ERR_NONE;
}

enum comm_status_t comm_send(int fd, const struct msg_header_t *hdr, const uint8_t *payload)
{
    enum comm_status_t result = COMM_ERR_NONE;
    msg_checksum_t cs = checksum(payload, HDR_GET_PAYLOAD_LENGTH(*hdr));

    struct msg_encoded_header_t hdr_encoded = raw_to_wire(hdr);

    //~ fprintf(stderr, "== sending ==\n");
    //~ comm_dump_message(hdr);
    //~ comm_dump_checksum(hdr, cs);

    result = comm_write_checked(fd, &hdr_encoded, sizeof(struct msg_encoded_header_t));
    if (result != COMM_ERR_NONE) {
        return result;
    }
    if (payload && (HDR_GET_PAYLOAD_LENGTH(*hdr) > 0)) {
        result = comm_write_checked(
            fd, payload, HDR_GET_PAYLOAD_LENGTH(*hdr));
        if (result != COMM_ERR_NONE) {
            return result;
        }
        result = comm_write_checked(fd, &cs, sizeof(msg_checksum_t));
        if (result != COMM_ERR_NONE) {
            return result;
        }
    }
    return result;
}

enum comm_status_t comm_send_ping(struct comm_t *comm)
{
    static const struct msg_header_t msg = HDR_INIT(
        MSG_ADDRESS_HOST,
        MSG_ADDRESS_LPC1114,
        0,
        MSG_FLAG_ECHO);
    return comm_send(comm->_fd, &msg, NULL);
}

enum comm_status_t comm_send_reset_message(struct comm_t *comm)
{
    comm_printf(comm, "sending reset message\n");
    // we’re advertising a payload which we won’t send completely
    // we also make sure to send an odd number of bytes
    static const struct msg_header_t hdr = HDR_INIT(
        MSG_ADDRESS_HOST,
        MSG_ADDRESS_LPC1114,
        0,
        MSG_FLAG_RESET);

    return comm_send(comm->_fd, &hdr, NULL);
}

enum comm_status_t comm_send_resync_message(struct comm_t *comm)
{
    comm_printf(comm, "sending resync message\n");
    // we’re advertising a payload which we won’t send completely
    // we also make sure to send an odd number of bytes
    static const struct msg_header_t hdr = HDR_INIT(
        MSG_ADDRESS_HOST,
        MSG_ADDRESS_LPC1114,
        13,
        0);
    struct msg_encoded_header_t enc = raw_to_wire(&hdr);
    static const uint8_t fake_payload[] = {0x00, 0x00};

    enum comm_status_t result = comm_write_checked(
        comm->_fd, &enc, sizeof(struct msg_encoded_header_t));
    if (result != COMM_ERR_NONE) {
        return result;
    }

    result = comm_write_checked(
        comm->_fd, &fake_payload[0], sizeof(fake_payload));

    return result;
}

void comm_thread_handle_packet(
    struct comm_t *comm,
    struct msg_header_t *hdr,
    uint8_t *payload)
{
    uint8_t *buffer = malloc(sizeof(struct msg_header_t)+
                             HDR_GET_PAYLOAD_LENGTH(*hdr));
    if (!buffer) {
        panicf("comm: failed to allocate memory for queue buffer\n");
        // does not return
    }
    memcpy(&buffer[0], hdr, sizeof(struct msg_header_t));
    memcpy(&buffer[sizeof(struct msg_header_t)], payload,
           HDR_GET_PAYLOAD_LENGTH(*hdr));

    queue_push(&comm->recv_queue, buffer);
    free(payload);
    send_char(comm->_recv_fd, COMM_PIPECHAR_MESSAGE);
}

void comm_thread_handle_unexpected_control(
    struct comm_t *comm,
    struct msg_header_t *hdr)
{
    comm_printf(comm, "unexpected control packet received\n");
    //~ comm_dump_header(hdr);
}

void comm_thread_signalfd(struct comm_t *comm, struct pollfd *signalfd)
{
    if (signalfd->revents & (POLLERR|POLLHUP)) {
        comm_printf(comm, "signalfd POLLERR|POLLHUP\n");
    } else if (signalfd->revents & POLLIN) {
        recv_char(comm->_signal_fd);
    }
}

bool comm_thread_state_open_tx(struct comm_t *state, uint8_t *buffer)
{
    struct msg_header_t *hdr = (struct msg_header_t*)buffer;
    enum comm_status_t status = comm_send(
        state->_fd, hdr, &buffer[sizeof(struct msg_header_t)]);
    switch (status) {
    case COMM_ERR_NONE:
    {
        break;
    }
    case COMM_ERR_DISCONNECTED:
    case COMM_ERR_TIMEOUT:
    {
        queue_push_front(&state->send_queue, buffer);
        comm_printf(state, "lost connection during send (%d)\n",
                           status);
        return false;
    }
    default:
    {
        panicf("comm: unexpected send result: %d\n", status);
    }
    }
    state->_pending_ack = buffer;
    timestamp_gettime(&state->_tx_timestamp);
    return true;
}

void comm_thread_state_closed(
    struct comm_t *comm,
    struct pollfd *datafd,
    bool timed_out)
{
    if (comm_open(comm) == 0) {
        comm->_conn_state = COMM_CONN_OPEN;
        datafd->fd = comm->_fd;
        comm_thread_to_state_open(comm);
        comm_timed_in_future(comm, 0);
    } else {
        comm_timed_in_future(comm, COMM_RECONNECT_TIMEOUT);
    }
}

void comm_thread_state_established(
    struct comm_t *comm,
    struct pollfd *datafd,
    bool timed_out)
{
    if (comm_check_datafd_error(comm, datafd)) {
        return;
    }

    if (datafd->revents & POLLIN) {
        struct msg_header_t hdr;
        uint8_t *payload = NULL;
        switch (comm_recv(comm->_fd, &hdr, &payload)) {
        case COMM_ERR_NONE:
        {
            comm_thread_handle_packet(comm, &hdr, payload);
            break;
        }
        case COMM_ERR_CONTROL:
        {
            if (HDR_GET_FLAGS(hdr) == MSG_FLAG_ACK) {
                free(comm->_pending_ack);
                comm->_pending_ack = NULL;
            } else {
                //~ comm_dump_message(&hdr);
                comm_thread_handle_unexpected_control(comm, &hdr);
            }
            break;
        }
        case COMM_ERR_CHECKSUM_ERROR:
        {
            comm_printf(comm, "checksum error\n");
            break;
        }
        case COMM_ERR_TIMEOUT:
        {
            comm_printf(comm, "timeout\n");
            comm_thread_to_state_out_of_sync(comm);
            comm_timed_in_future(comm, 0);
            return;
        }
        case COMM_ERR_DISCONNECTED:
        {
            comm_printf(comm, "disconnected\n");
            return;
        }
        default:
        {
            panicf("comm[established]: unhandled recv status\n");
        }
        }
    }

    int32_t timeout = -1;
    if (comm->_pending_ack == NULL) {
        if (!queue_empty(&comm->send_queue)) {
            uint8_t *buffer = queue_pop(&comm->send_queue);
            assert(buffer);
            if (!comm_thread_state_open_tx(comm, buffer)) {
                return;
            }
            timeout = COMM_RETRANSMISSION_TIMEOUT;
            comm->_retransmission_counter = 0;
        }
    } else {
        struct timespec curr_time;
        timestamp_gettime(&curr_time);
        int32_t dt = timestamp_delta_in_msec(
            &curr_time,
            &comm->_tx_timestamp);
        if (dt < COMM_RETRANSMISSION_TIMEOUT) {
            timeout = COMM_RETRANSMISSION_TIMEOUT - dt;
        } else {
            timeout = 0;
        }
        //~ comm_printf(comm, "rtx timeout = %d (dt=%d)\n", timeout, dt);
        if (timeout <= 0) {
            if (comm->_retransmission_counter >= COMM_MAX_RETRANSMISSION)
            {
                comm_printf(comm, "retransmission counter reached "
                                  "maximum\n");
                comm_thread_to_state_open(comm);
                comm_timed_in_future(comm, 0);
                return;
            }
            comm_printf(comm, "retransmission\n");
            if (!comm_thread_state_open_tx(comm, comm->_pending_ack)) {
                comm->_pending_ack = NULL;
                return;
            }
            timeout = COMM_RETRANSMISSION_TIMEOUT;
            comm->_retransmission_counter++;
        }
    }

    if (timeout >= 0) {
        comm_timed_in_future(comm, timeout);
    } else {
        comm_timed_disable(comm);
    }
}

void comm_thread_state_open(
    struct comm_t *comm,
    struct pollfd *datafd,
    bool timed_out)
{
    if (comm_check_datafd_error(comm, datafd)) {
        return;
    }

    if (comm->_sync.ping_counter == 0) {
        comm_send_resync_message(comm);
        comm->_sync.ping_counter = 1;
        // make sure to wait some time so that the read on the μC
        // can time out
        comm_timed_in_future(comm, COMM_RETRANSMISSION_TIMEOUT*2);
        timed_out = false;
    }

    if (datafd->revents & POLLIN) {
        struct msg_header_t hdr;
        uint8_t *payload = NULL;
        switch (comm_recv(comm->_fd, &hdr, &payload))
        {
        case COMM_ERR_NONE:
        {
            comm_printf(comm, "received data packet\n");
            comm_thread_handle_packet(comm, &hdr, payload);
            break;
        }
        case COMM_ERR_DISCONNECTED:
        {
            comm_printf(comm, "disconnected\n");
            comm_thread_to_state_closed(comm);
            comm_timed_in_future(comm, 0);
            return;
        }
        case COMM_ERR_TIMEOUT:
        case COMM_ERR_PROTOCOL_VIOLATION:
        {
            comm_printf(comm, "timeout or protocol violation\n");
            // timeouts can happen during resync!
            // protocol violations might be due to out of syncness
            // we wait until the next retransmission timeout
            break;
        }
        case COMM_ERR_FLAGS:
        {
            comm_printf(comm, "unexpected flags in data packet\n");
            break;
        }
        case COMM_ERR_CHECKSUM_ERROR:
        {
            comm_printf(comm, "checksum error\n");
            break;
        }
        case COMM_ERR_CONTROL:
        {
            if ((HDR_GET_FLAGS(hdr) & (MSG_FLAG_ECHO|MSG_FLAG_ACK)))
            {
                // ping response
                comm_thread_to_state_established(comm);
                comm_timed_in_future(comm, 0);
                return;
            }
            comm_thread_handle_unexpected_control(comm, &hdr);
            // wait until next retransmission timeout
            break;
        }
        }
    } else if (timed_out) {
        comm_printf(comm, "sending another ping\n");
        comm_timed_in_future(comm, COMM_RETRANSMISSION_TIMEOUT);
        comm->_sync.ping_counter = (comm->_sync.ping_counter+1) % COMM_MAX_RETRANSMISSION;
        const enum comm_status_t status = comm_send_ping(comm);
        switch (status)
        {
        case COMM_ERR_NONE:
        {
            break;
        }
        case COMM_ERR_DISCONNECTED:
        {
            comm_printf(comm, "disconnected\n");
            comm_thread_to_state_closed(comm);
            comm_timed_in_future(comm, 0);
            return;
        }
        case COMM_ERR_TIMEOUT:
        {
            comm_printf(comm, "timeout\n");
            // transmission timeout??
            comm_thread_to_state_closed(comm);
            comm_timed_in_future(comm, 0);
            return;
        }
        default:
        {
            panicf("comm[open]: unexpected send status: %d\n", status);
        }
        };

    }
}

void comm_thread_state_out_of_sync(
    struct comm_t *comm,
    struct pollfd *datafd,
    bool timed_out)
{
    if (comm_check_datafd_error(comm, datafd)) {
        return;
    }

    comm_thread_to_state_open(comm);
    comm_timed_in_future(comm, 0);
}

void comm_thread_to_state_closed(struct comm_t *comm)
{
    if (comm->_fd >= 0) {
        close(comm->_fd);
        comm->_fd = -1;
    }
    if (comm->_conn_state == COMM_CONN_ESTABLISHED) {
        send_char(comm->_recv_fd, COMM_PIPECHAR_FAILED);
    }
    fprintf(stderr, "comm[%s] -> comm[%s]\n",
                    comm_conn_state_str(comm->_conn_state),
                    comm_conn_state_str(COMM_CONN_CLOSED));
    comm->_conn_state = COMM_CONN_CLOSED;
}

void comm_thread_to_state_established(struct comm_t *comm)
{
    if (comm->_conn_state != COMM_CONN_ESTABLISHED) {
        send_char(comm->_recv_fd, COMM_PIPECHAR_READY);
    }
    comm->_retransmission_counter = 0;
    fprintf(stderr, "comm[%s] -> comm[%s]\n",
                    comm_conn_state_str(comm->_conn_state),
                    comm_conn_state_str(COMM_CONN_ESTABLISHED));
    comm->_conn_state = COMM_CONN_ESTABLISHED;
    comm_send_reset_message(comm);
}

void comm_thread_to_state_open(struct comm_t *comm)
{
    if (comm->_conn_state == COMM_CONN_ESTABLISHED) {
        send_char(comm->_recv_fd, COMM_PIPECHAR_FAILED);
    }
    fprintf(stderr, "comm[%s] -> comm[%s]\n",
                    comm_conn_state_str(comm->_conn_state),
                    comm_conn_state_str(COMM_CONN_OPEN));
    comm->_conn_state = COMM_CONN_OPEN;
    comm->_sync.ping_counter = 0;
}

void comm_thread_to_state_out_of_sync(struct comm_t *comm)
{
    if (comm->_conn_state == COMM_CONN_ESTABLISHED) {
        send_char(comm->_recv_fd, COMM_PIPECHAR_FAILED);
    }
    fprintf(stderr, "comm[%s] -> comm[%s]\n",
                    comm_conn_state_str(comm->_conn_state),
                    comm_conn_state_str(COMM_CONN_OUT_OF_SYNC));
    comm->_conn_state = COMM_CONN_OUT_OF_SYNC;
}

void *comm_thread(struct comm_t *comm)
{
    struct pollfd pollfds[2];
    pollfds[0].fd = comm->_signal_fd;
    pollfds[0].events = POLLIN;
    pollfds[0].revents = 0;
    pollfds[1].events = POLLIN;
    pollfds[1].revents = 0;

    comm_thread_to_state_closed(comm);
    comm_timed_in_future(comm, 0);

    pthread_mutex_lock(&comm->data_mutex);
    while (!comm->terminated)
    {
        pthread_mutex_unlock(&comm->data_mutex);
        int timeout = -1;
        if (comm->_timed_event.active) {
            struct timespec curr_time;
            timestamp_gettime(&curr_time);
            timeout = timestamp_delta_in_msec(
                &comm->_timed_event.next,
                &curr_time);
            if (timeout < 0) {
                timeout = 0;
            }
        }
        int result = poll(&pollfds[0],
             (comm->_conn_state != COMM_CONN_CLOSED ? 2 : 1),
             timeout);
        bool timed_out = true;
        if (result < 0) {
            panicf("comm: poll returned error: %d (%s)\n",
                   errno, strerror(errno));
        } else {
            timed_out = result == 0;
        }
        comm_thread_signalfd(comm, &pollfds[0]);
        switch (comm->_conn_state)
        {
        case COMM_CONN_CLOSED:
        {
            comm_thread_state_closed(comm, &pollfds[1], timed_out);
            break;
        }
        case COMM_CONN_OPEN:
        {
            comm_thread_state_open(comm, &pollfds[1], timed_out);
            break;
        }
        case COMM_CONN_ESTABLISHED:
        {
            comm_thread_state_established(comm, &pollfds[1], timed_out);
            break;
        }
        case COMM_CONN_OUT_OF_SYNC:
        {
            comm_thread_state_out_of_sync(comm, &pollfds[1], timed_out);
            break;
        }
        default:
        {
            panicf("comm: invalid state: %d\n", comm->_conn_state);
        }
        }
        pthread_mutex_lock(&comm->data_mutex);
    }
    pthread_mutex_unlock(&comm->data_mutex);

    return NULL;
}

void comm_timed_disable(struct comm_t *comm)
{
    comm->_timed_event.active = false;
}

void comm_timed_in_future(struct comm_t *comm, int32_t msec)
{
    timestamp_gettime_in_future(
        &comm->_timed_event.next,
        msec);
    comm->_timed_event.active = true;
}

char comm_wait(struct pollfd *pollfds, int timeout)
{
    if (poll(&pollfds[0], 1, timeout) == 1) {
        if (pollfds[0].revents & POLLIN) {
            char result;
            read(pollfds[0].fd, &result, 1);
            return result;
        }
    }
    return '\0';
}

enum comm_status_t comm_write_checked(int fd, const void *const buf, intptr_t len)
{
    struct pollfd pollfd;
    pollfd.fd = fd;
    pollfd.events = POLLOUT;
    pollfd.revents = 0;

    const uint8_t *buffer = buf;
    intptr_t written_total = 0;
    while (written_total < len) {
        int status = poll(&pollfd, 1, COMM_WRITE_TIMEOUT);
        if (status == 0) {
            return COMM_ERR_TIMEOUT;
        }
        if (pollfd.revents & (POLLERR|POLLHUP)) {
            return COMM_ERR_DISCONNECTED;
        } else if (pollfd.revents & POLLOUT) {
            intptr_t written_this_time = write(
                fd, buffer, len - written_total);
            //~ fprintf(stdout, ">> ");
            //~ dump_buffer(stdout, buffer, written_this_time);
            assert(written_this_time > 0);
            written_total += written_this_time;
            buffer += written_this_time;
        }
    }
    return COMM_ERR_NONE;
}
