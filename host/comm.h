#ifndef _COMM_H
#define _COMM_H

#include <pthread.h>
#include <stdint.h>

#include "common/comm.h"
#include "queue.h"

#define RECONNECT_TIMEOUT (3000)
#define READ_TIMEOUT (100)

enum comm_status_t
{
    COMM_ERR_NONE,
    COMM_ERR_CHECKSUM_ERROR,
    COMM_ERR_DISCONNECTED,
    COMM_ERR_TIMEOUT,
    COMM_ERR_PROTOCOL_VIOLATION
};

struct comm_t
{
    pthread_t thread;

    // public
    pthread_mutex_t data_mutex;
    int signal_fd, recv_fd;
    bool terminated;
    struct queue_t send_queue;
    struct queue_t recv_queue;

    // private
    char *_devfile;
    int _fd, _signal_fd;
    int _recv_fd;
    uint32_t _baudrate;
};

void comm_init(
    struct comm_t *comm,
    const char *devfile,
    uint32_t baudrate);

void *comm_thread(struct comm_t *state);

#endif
