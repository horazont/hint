#include "broker.h"

#include "common/comm_lpc1114.h"

#include <poll.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

void broker_init(struct broker_t *broker, struct comm_t *comm)
{
    broker->comm = comm;

    pthread_create(
        &broker->thread, NULL,
        (void*(*)(void*))&broker_thread,
        broker);
}

void broker_process_lpc_message(struct lpc_msg_t *msg)
{
    switch (msg->subject) {
    case LPC_SUBJECT_TOUCH_EVENT:
    {
        fprintf(stderr, "broker: touch event: x=%d; y=%d; z=%d\n",
                msg->payload.touch_ev.x,
                msg->payload.touch_ev.y,
                msg->payload.touch_ev.z);
        break;
    }
    default:
    {
        fprintf(stderr, "broker: unknown subject in lpc message: %d\n",
                        msg->subject);
        break;
    }
    }
}

void broker_process_message(void *item)
{
    struct msg_header_t *hdr = (struct msg_header_t *)item;
    switch (hdr->sender) {
    case MSG_ADDRESS_HOST:
    {
        fprintf(stderr, "broker: received message from meself\n");
        break;
    }
    case MSG_ADDRESS_LPC1114:
    {
        struct lpc_msg_t msg;
        memcpy(&msg, &((uint8_t*)item)[sizeof(struct msg_header_t)],
               hdr->payload_length);
        free(item);
        broker_process_lpc_message(&msg);
        return;
    }
    case MSG_ADDRESS_ARDUINO:
    {
        fprintf(stderr, "broker: received message from arduino, cannot handle\n");
        break;
    }
    default:
    {
        fprintf(stderr, "broker: unknown sender address: %d\n",
                        hdr->sender);
        break;
    }
    }
    comm_dump_message(hdr);
    free(item);
}

void *broker_thread(struct broker_t *state)
{
#define FD_COUNT 1
#define FD_RECV 0
    struct pollfd pollfds[FD_COUNT];
    pollfds[0].fd = state->comm->recv_fd;
    pollfds[0].events = POLLIN;
    pollfds[0].revents = 0;

    while (true)
    {
        poll(&pollfds[0], FD_COUNT, -1);

        if (pollfds[FD_RECV].revents & POLLIN) {
            char act;
            read(pollfds[FD_RECV].fd, &act, 1);
            if (queue_empty(&state->comm->recv_queue)) {
                fprintf(stderr, "broker: BUG: recv trigger received, "
                                "but queue is empty!\n");
                continue;
            }
            void *item = queue_pop(&state->comm->recv_queue);
            assert(item != NULL);
            broker_process_message(item);
        }
    }

    return NULL;
}
