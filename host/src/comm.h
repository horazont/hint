#ifndef _COMM_H
#define _COMM_H

#include <pthread.h>
#include <stdint.h>

#include "common/comm.h"
#include "queue.h"

#define RECONNECT_TIMEOUT (3000)
#define READ_TIMEOUT (100)
#define WRITE_TIMEOUT (READ_TIMEOUT*2)
#define RETRANSMISSION_TIMEOUT (500)

enum comm_status_t
{
    //! everything went fine
    COMM_ERR_NONE,

    //! checksum validation failed on received data
    COMM_ERR_CHECKSUM_ERROR,

    //! usb device disconnected while sending or receiving
    COMM_ERR_DISCONNECTED,

    //! timeout while sending or receiving
    COMM_ERR_TIMEOUT,

    //! constraints of protocol were violated
    COMM_ERR_PROTOCOL_VIOLATION,

    //! a control packet was successfully received instead of a data
    //! packet
    COMM_ERR_CONTROL
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
    uint8_t *_pending_ack;
    uint8_t _attempt;
    struct timespec _tx_timestamp;
};

void *comm_alloc_message(
    const msg_address_t recipient,
    const msg_length_t payload_length);

void comm_dump_message(const struct msg_header_t *item);

void comm_init(
    struct comm_t *comm,
    const char *devfile,
    uint32_t baudrate);

void comm_enqueue_msg(struct comm_t *comm, void *msg);

void *comm_thread(struct comm_t *state);

#endif
