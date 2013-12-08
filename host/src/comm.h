#ifndef _COMM_H
#define _COMM_H

#include <pthread.h>
#include <stdint.h>

#include "common/comm.h"
#include "queue.h"
#include "timestamp.h"

#ifndef COMM_RECONNECT_TIMEOUT
#define COMM_RECONNECT_TIMEOUT          (3000)
#endif

#ifndef COMM_PING_TIMEOUT
#define COMM_PING_TIMEOUT               (250)
#endif

#ifndef COMM_READ_TIMEOUT
#define COMM_READ_TIMEOUT               (100)
#endif

#ifndef COMM_WRITE_TIMOUT
#define COMM_WRITE_TIMEOUT              (COMM_READ_TIMEOUT*2)
#endif

#ifndef COMM_RETRANSMISSION_TIMEOUT
#define COMM_RETRANSMISSION_TIMEOUT     (500)
#endif

#ifndef COMM_MAX_RETRANSMISSION
#define COMM_MAX_RETRANSMISSION         (3)
#endif

#define COMM_PIPECHAR_READY             ('r')
#define COMM_PIPECHAR_FAILED            ('f')
#define COMM_PIPECHAR_MESSAGE           ('m')

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
    COMM_ERR_CONTROL,

    //! a data packet was received, but it contained unexpected flags
    COMM_ERR_FLAGS
};

enum comm_conn_state_t {
    //! serial device node not open
    COMM_CONN_CLOSED,

    //! serial device open, not verified yet that LPC is on the other
    //! side
    COMM_CONN_OPEN,

    //! serial device open, LPC pings
    COMM_CONN_ESTABLISHED,

    //! serial device open, a reception timed out
    COMM_CONN_OUT_OF_SYNC
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
    uint8_t _retransmission_counter;
    uint8_t _attempt;
    struct timespec _tx_timestamp;
    struct {
        bool active;
        struct timespec next;
    } _timed_event;
    bool _pending_ping;

    enum comm_conn_state_t _conn_state;
};

void *comm_alloc_message(
    const msg_address_t recipient,
    const msg_length_t payload_length);
void comm_dump_message(const struct msg_header_t *item);
void comm_enqueue_msg(struct comm_t *comm, void *msg);
void comm_init(
    struct comm_t *comm,
    const char *devfile,
    uint32_t baudrate);
bool comm_is_available(struct comm_t *comm);
void *comm_thread(struct comm_t *state);

#endif
