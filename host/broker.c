#include <poll.h>
#include <assert.h>

void broker_init(struct broker_state_t *broker, struct comm_state_t *comm)
{
    broker->comm = comm;

    pthread_create(
        &broker->thread, NULL,
        (void*(*)(void*))&broker_thread,
        comm);
}

void broker_process_message(void *item)
{

}

void *broker_thread(struct borker_state_t *state)
{
#define FD_COUNT 1
#define FD_RECV 0
    struct pollfd pollfds[FD_COUNT];
    pollfds[0].fd = state->comm->recv_fd;
    pollfds[0].events = POLLIN;
    pollfds[0].revents = 0;

    while (true)
    {
        assert(poll(&pollfds[0], FD_COUNT, -1) >= 0);

        if (pollfds[FD_RECV].revents & POLLIN) {
            char act;
            assert(read(pollfds[FD_RECV].fd, &act, 1) == 1);
            void *item = queue_pop(state->comm->recv_queue);
            assert(item != NULL);
            broker_process_message(item);
        }
    }

    return NULL;
}
