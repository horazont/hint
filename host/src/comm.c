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

#include "utils.h"
#include "timestamp.h"

enum comm_state_t
{
    COMM_CLOSED = 0,
    COMM_OPEN
};

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

void comm_dump_header(const struct msg_header_t *hdr)
{
    fprintf(stderr, "dumping message@%08lx: header: \n", (uint64_t)hdr);
    dump_buffer(stderr, (const uint8_t*)hdr, sizeof(struct msg_header_t));
}

void comm_dump_body(const struct msg_header_t *hdr, const uint8_t *buffer)
{
    fprintf(stderr, "        message@%08lx: payload: \n", (uint64_t)hdr);
    dump_buffer(stderr, buffer, HDR_GET_PAYLOAD_LENGTH(*hdr));
}

void comm_dump_message(const struct msg_header_t *item)
{
    comm_dump_header(item);
    comm_dump_body(item, &((const uint8_t*)item)[sizeof(struct msg_header_t)]);
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
    comm->_fd = 0;
    comm->_baudrate = baudrate;
    comm->_pending_ack = NULL;

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
    result = comm->_fd != 0;
    pthread_mutex_unlock(&comm->data_mutex);
    return result;
}

void comm_enqueue_msg(struct comm_t *comm, void *msg)
{
    queue_push(&comm->send_queue, msg);
    write(comm->signal_fd, "m", 1);
}

int _comm_open(struct comm_t *state)
{
    int fd = open(state->_devfile, O_RDWR|O_CLOEXEC|O_NOCTTY);
    if (fd < 0) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        fprintf(stderr, "fcntl(comm_device, F_SETFL, O_NONBLOCK) failed: %d: %s\n", errno, strerror(errno));
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

enum comm_status_t _comm_write_checked(int fd, const void *const buf, intptr_t len)
{
    struct pollfd pollfd;
    pollfd.fd = fd;
    pollfd.events = POLLOUT;
    pollfd.revents = 0;

    const uint8_t *buffer = buf;
    intptr_t written_total = 0;
    while (written_total < len) {
        int status = poll(&pollfd, 1, WRITE_TIMEOUT);
        if (status == 0) {
            return COMM_ERR_TIMEOUT;
        }
        if (pollfd.revents & (POLLERR|POLLHUP)) {
            return COMM_ERR_DISCONNECTED;
        } else if (pollfd.revents & POLLOUT) {
            intptr_t written_this_time = write(
                fd, buffer, len - written_total);
            assert(written_this_time > 0);
            written_total += written_this_time;
            buffer += written_this_time;
        }
    }
    return COMM_ERR_NONE;
}

enum comm_status_t _comm_read_checked(int fd, void *const buf, intptr_t len)
{
    struct pollfd pollfd;
    pollfd.fd = fd;
    pollfd.events = POLLIN;
    pollfd.revents = 0;

    intptr_t read_total = 0;
    uint8_t *buffer = buf;
    while (read_total < len) {
        int status = poll(&pollfd, 1, READ_TIMEOUT);
        if (status == 0) {
            return COMM_ERR_TIMEOUT;
        }
        if (pollfd.revents & (POLLERR|POLLHUP)) {
            return COMM_ERR_DISCONNECTED;
        } else if (pollfd.revents & POLLIN) {
            intptr_t read_this_time = read(fd, buffer, len - read_total);
            assert(read_this_time > 0);
            read_total += read_this_time;
            buffer += read_this_time;
        }
    }
    return COMM_ERR_NONE;
}

bool _comm_send(int fd, const struct msg_header_t *hdr, const uint8_t *payload)
{
    enum comm_status_t result = COMM_ERR_NONE;
    msg_checksum_t cs = checksum(payload, HDR_GET_PAYLOAD_LENGTH(*hdr));

    struct msg_header_t hdr_encoded = {hdr->data};
    header_to_wire(&hdr_encoded);

    result = _comm_write_checked(fd, &hdr_encoded, sizeof(struct msg_header_t));
    if (result != COMM_ERR_NONE) {
        return result;
    }
    result = _comm_write_checked(
        fd, payload, HDR_GET_PAYLOAD_LENGTH(*hdr));
    if (result != COMM_ERR_NONE) {
        return result;
    }
    result = _comm_write_checked(fd, &cs, sizeof(msg_checksum_t));
    if (result != COMM_ERR_NONE) {
        return result;
    }
    return true;
}

enum comm_status_t _comm_recv(int fd, struct msg_header_t *hdr, uint8_t **payload)
{
    enum comm_status_t result = _comm_read_checked(
        fd, hdr, sizeof(struct msg_header_t));
    if (result != COMM_ERR_NONE) {
        return result;
    }

    wire_to_header(hdr);

    const msg_length_t length = HDR_GET_PAYLOAD_LENGTH(*hdr);

    if (length > MSG_MAX_PAYLOAD) {
        return COMM_ERR_PROTOCOL_VIOLATION;
    }

    if (length == 0) {
        *payload = NULL;
        return COMM_ERR_CONTROL;
    }

    *payload = malloc(length);

    result = _comm_read_checked(fd, *payload, length);
    if (result != COMM_ERR_NONE) {
        free(*payload);
        *payload = NULL;
        return result;
    }
    msg_checksum_t checksum = 0;
    result = _comm_read_checked(fd, &checksum, sizeof(msg_checksum_t));
    if (result != COMM_ERR_NONE) {
        free(*payload);
        *payload = NULL;
        return result;
    }

    // TODO: validate checksum

    return COMM_ERR_NONE;
}

char _comm_wait(struct pollfd *pollfds, int timeout)
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

bool _comm_thread_state_open_tx(struct comm_t *state, uint8_t *buffer)
{
    struct msg_header_t *hdr = (struct msg_header_t*)buffer;
    if (!_comm_send(state->_fd, hdr, &buffer[sizeof(struct msg_header_t)])) {
        queue_push_front(&state->send_queue, buffer);
        fprintf(stderr, "comm: lost connection during send\n");
        return false;
    }
    state->_pending_ack = buffer;
    timestamp_gettime(&state->_tx_timestamp);
    return true;
}

bool _comm_thread_state_open(struct comm_t *state, struct pollfd pollfds[2])
{
    if (state->_pending_ack == NULL) {
        if (!queue_empty(&state->send_queue)) {
            fprintf(stderr, "comm: debug: starting tx\n");
            uint8_t *buffer = queue_pop(&state->send_queue);
            assert(buffer);
            if (!_comm_thread_state_open_tx(state, buffer)) {
                return false;
            }
        }
    }

    int timeout = -1;
    if (state->_pending_ack != NULL) {
        struct timespec curr_time;
        timestamp_gettime(&curr_time);
        uint32_t dt = timestamp_delta_in_msec(&curr_time, &state->_tx_timestamp);
        if (dt < RETRANSMISSION_TIMEOUT) {
            timeout = RETRANSMISSION_TIMEOUT - dt;
        } else {
            timeout = 0;
        }
    } else if (!queue_empty(&state->send_queue)) {
        timeout = 0;
    }

    poll(&pollfds[0], 2, timeout);
    if (pollfds[1].revents & (POLLERR|POLLHUP)) {
        fprintf(stderr, "comm: disconnected\n");
        return false;
    }

    if (pollfds[1].revents & (POLLIN)) {
        struct msg_header_t hdr;
        uint8_t *payload = NULL;
        uint8_t *buffer = NULL;
        switch (_comm_recv(state->_fd, &hdr, &payload)) {
        case COMM_ERR_NONE:
        {
            buffer = malloc(sizeof(struct msg_header_t)+
                            HDR_GET_PAYLOAD_LENGTH(hdr));
            assert(buffer);
            memcpy(&buffer[0], &hdr, sizeof(struct msg_header_t));
            memcpy(&buffer[sizeof(struct msg_header_t)], payload,
                   HDR_GET_PAYLOAD_LENGTH(hdr));
            queue_push(&state->recv_queue, buffer);
            free(payload);
            send_char(state->_recv_fd, COMM_PIPECHAR_MESSAGE);
            break;
        }
        case COMM_ERR_CONTROL:
        {
            if (HDR_GET_FLAGS(hdr) == MSG_FLAG_ACK) {
                //~ fprintf(stderr, "comm: debug: ack received\n");
                free(state->_pending_ack);
                state->_pending_ack = NULL;
            } else {
                comm_dump_message(&hdr);
                fprintf(stderr, "comm: unknown control flags: %02x\n",
                        HDR_GET_FLAGS(hdr));
            }
            break;
        }
        case COMM_ERR_CHECKSUM_ERROR:
        {
            fprintf(stderr, "comm: checksum error\n");
            break;
        }
        case COMM_ERR_TIMEOUT:
        {
            fprintf(stderr, "comm: timeout\n");
            break;
        }
        case COMM_ERR_DISCONNECTED:
        {
            fprintf(stderr, "comm: lost connection during recv\n");
            return false;
        }
        default:
        {
            fprintf(stderr, "comm: unhandled recv status\n");
            assert(false);
        }
        }
    }

    if (pollfds[0].revents & (POLLERR|POLLHUP)) {
        fprintf(stderr, "comm: trigger pipe closed.\n");
    } else if (pollfds[0].revents  & (POLLIN)) {
        char w;
        read(pollfds[0].fd, &w, 1);
    }

    if (state->_pending_ack != NULL)
    {
        struct timespec curr_time;
        timestamp_gettime(&curr_time);
        uint32_t dt = timestamp_delta_in_msec(&curr_time, &state->_tx_timestamp);
        if (dt >= RETRANSMISSION_TIMEOUT) {
            fprintf(stderr, "comm: retransmission\n");
            _comm_thread_state_open_tx(state, state->_pending_ack);
        }
    }

    return true;
}

void *comm_thread(struct comm_t *state)
{
    enum comm_state_t sm =
        (state->_fd == 0 ? COMM_CLOSED : COMM_OPEN);

    struct pollfd pollfds[2];
    pollfds[0].fd = state->_signal_fd;
    pollfds[0].events = POLLIN;
    pollfds[0].revents = 0;
    pollfds[1].events = POLLIN;
    pollfds[1].revents = 0;

    pthread_mutex_lock(&state->data_mutex);
    while (!state->terminated)
    {
        pthread_mutex_unlock(&state->data_mutex);
        switch (sm)
        {
        case COMM_CLOSED:
        {
            if (_comm_open(state) == 0) {
                sm = COMM_OPEN;
                pollfds[1].fd = state->_fd;
                fprintf(stderr, "comm: opened serial device\n");
                // signal enabling of serial device
                send_char(state->_recv_fd, COMM_PIPECHAR_READY);
            } else {
                fprintf(stderr, "comm: open of `%s' failed, will try "
                        "again in %d ms\n",
                        state->_devfile,
                        RECONNECT_TIMEOUT);
                _comm_wait(&pollfds[0], RECONNECT_TIMEOUT);
            }
            break;
        }
        case COMM_OPEN:
        {
            if (!_comm_thread_state_open(state, pollfds)) {
                fprintf(stderr, "comm: disconnected\n");
                // disconnect happened
                sm = COMM_CLOSED;
                close(state->_fd);
                state->_fd = 0;
                if (state->_pending_ack) {
                    queue_push_front(
                        &state->recv_queue, state->_pending_ack);
                    state->_pending_ack = NULL;
                }
            }
            break;
        }
        default:
            assert(false);
        }
        pthread_mutex_lock(&state->data_mutex);
    }
    pthread_mutex_unlock(&state->data_mutex);

    return NULL;
}
