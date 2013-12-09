#ifndef _COMM_INTERNAL_H
#define _COMM_INTERNAL_H

#define MSG_QUEUE_SIZE 2

struct port_queue_item_t {
    bool empty;
    volatile bool *flag_to_clear;
    const volatile struct msg_header_t *header;
    const volatile uint8_t *data;
    uint8_t checksum;
};

struct comm_port_t {
    struct {
        struct msg_header_t curr_header;
        volatile uint8_t *recv_dest;
        volatile uint8_t *recv_end;
        volatile const uint8_t *trns_src;
        volatile const uint8_t *trns_end;
        uint16_t recv_checksum_A;
        uint16_t recv_checksum_B;
        msg_length_t remaining;
        volatile struct msg_buffer_t *dest_msg;
    } state;

    struct msg_buffer_t route_buffer;

    struct port_queue_item_t queue[MSG_QUEUE_SIZE];
    int active_queue;
};

#endif
