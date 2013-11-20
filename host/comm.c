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

enum comm_state_t
{
    COMM_CLOSED = 0,
    COMM_OPEN
};

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
    assert(pipe(fds) == 0);

    comm->_devfile = strdup(devfile);
    comm->_fd = 0;
    comm->_signal_fd = fds[0];
    comm->_baudrate = baudrate;

    comm->terminated = false;
    comm->signal_fd = fds[1];

    assert(pipe(fds) == 0);
    comm->_recv_fd = fds[1];
    comm->recv_fd = fds[0];

    queue_init(&comm->send_queue);
    queue_init(&comm->recv_queue);

    pthread_mutex_init(&comm->data_mutex, NULL);
    pthread_create(&comm->thread, NULL, (void*(*)(void*))&comm_thread, &comm);
}

int _comm_open(struct comm_t *state)
{
    int fd = open(state->_devfile, O_CLOEXEC|O_NOCTTY);
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

    state->_fd = fd;
    return 0;
}

bool _comm_write_checked(int fd, const void *const buf, intptr_t len)
{
    const uint8_t *buffer = buf;
    intptr_t written = 0;
    do {
        intptr_t written_this_time = write(fd, buffer, len - written);
        if (written_this_time == 0) {
            return false;
        }
        written += written_this_time;
        buffer += written_this_time;
    } while (written < len);
    return true;
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
    if (!_comm_write_checked(fd, hdr, sizeof(struct msg_header_t))) {
        return false;
    }
    if (!_comm_write_checked(fd, payload, hdr->payload_length)) {
        return false;
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

    if (hdr->payload_length > MSG_MAX_PAYLOAD) {
        return COMM_ERR_PROTOCOL_VIOLATION;
    }

    *payload = malloc(hdr->payload_length);

    result = _comm_read_checked(fd, *payload, hdr->payload_length);
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
            } else {
                _comm_wait(&pollfds[0], RECONNECT_TIMEOUT);
            }
            break;
        }
        case COMM_OPEN:
        {
            while (!queue_empty(&state->send_queue)) {
                uint8_t *buffer = queue_pop(&state->send_queue);
                struct msg_header_t *hdr = (struct msg_header_t*)buffer;
                if (!_comm_send(state->_fd, hdr, &buffer[sizeof(struct msg_header_t)])) {
                    free(buffer);
                    fprintf(stderr, "comm: lost connection during send\n");
                    sm = COMM_CLOSED;
                    close(state->_fd);
                    state->_fd = 0;
                    break;
                }
                free(buffer);
            }
            if (sm != COMM_OPEN) {
                break;
            }
            poll(&pollfds[0], 2, -1);
            if (pollfds[1].revents & (POLLERR|POLLHUP)) {
                // error, disconnected
            } else if (pollfds[1].revents & (POLLIN)) {
                struct msg_header_t hdr;
                uint8_t **payload = NULL;
                uint8_t *buffer = NULL;
                enum comm_status_t status = _comm_recv(state->_fd, &hdr, payload);
                if (status == COMM_ERR_NONE) {
                    buffer = malloc(sizeof(struct msg_header_t)+
                                    hdr.payload_length);
                    assert(buffer);
                    memcpy(&buffer[0], &hdr, sizeof(struct msg_header_t));
                    memcpy(&buffer[sizeof(struct msg_header_t)], *payload, hdr.payload_length);
                    queue_push(&state->recv_queue, buffer);
                    free(*payload);
                    write(state->_recv_fd, 'p', 1);
                } else if (status == COMM_ERR_CHECKSUM_ERROR) {
                    fprintf(stderr, "comm: checksum error\n");
                } else if (status == COMM_ERR_TIMEOUT) {
                    fprintf(stderr, "comm: timeout\n");
                } else if (status == COMM_ERR_DISCONNECTED) {
                    fprintf(stderr, "comm: lost connection during recv\n");
                    sm = COMM_CLOSED;
                    close(state->_fd);
                    state->_fd = 0;
                    break;
                }
            }
            if (pollfds[0].revents & (POLLERR|POLLHUP)) {
            } else if (pollfds[0].revents  & (POLLIN)) {
                // trigger
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
