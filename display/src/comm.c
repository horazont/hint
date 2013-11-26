#include "comm.h"

#include "time.h"
#include "config.h"

#include <string.h>

#include "comm_internal.h"
#include "comm_common.inc.c"
#include "comm_uart.inc.c"

void comm_init(const uint32_t uart_baudrate)
{
    for (int i = 0; i < 2; i++) {
        appbuffer[i].in_use = false;
    }
    uart.route_buffer.in_use = false;
    frontbuffer_locked = false;

    uart_init(uart_baudrate);
}

struct msg_buffer_t *comm_get_rx_message()
{
    NVIC_DisableIRQ(UART_IRQn);
    bool locked = frontbuffer_locked;
    NVIC_EnableIRQ(UART_IRQn);
    if (!locked) {
        return NULL;
    }
    // we can cast away volatile here, as long as the buffer is locked
    return (struct msg_buffer_t*)appbuffer_front;
}

bool comm_release_rx_message()
{
    bool available = false;
    NVIC_DisableIRQ(UART_IRQn);
    frontbuffer_locked = false;
    appbuffer_front->in_use = false;
    if (backbuffer_ready) {
        swap_app_buffers();
        backbuffer_ready = false;
        available = frontbuffer_locked = true;
    }
    NVIC_EnableIRQ(UART_IRQn);
    return available;
}

static inline enum msg_status_t comm_tx_message_uart(
    const struct msg_header_t *hdr,
    const uint8_t *payload,
    const msg_checksum_t checksum)
{
    volatile bool pending = true;
    NVIC_DisableIRQ(UART_IRQn);
    while (!comm_enqueue_tx_nowait(
            &uart,
            hdr,
            payload,
            checksum,
            &pending))
    {
        NVIC_EnableIRQ(UART_IRQn);
        // wait for interrupt
        __asm__ volatile ("wfi");
        NVIC_DisableIRQ(UART_IRQn);
    }
    uart_tx_trigger();
    NVIC_EnableIRQ(UART_IRQn);
    while (pending) {
        // pending will be set false after the message has left the
        // queue and went over the wire
        __asm__ volatile("wfi");
    }
    return MSG_NO_ERROR;
}

enum msg_status_t comm_tx_ack(const msg_address_t recipient)
{
    struct msg_header_t hdr = {
        .sender = MSG_ADDRESS_LPC1114,
        .recipient = recipient,
        .payload_length = 0,
        .flags = MSG_FLAG_ACK
        };
    return comm_tx_message(&hdr, NULL, 0);
}

enum msg_status_t comm_tx_nak(
    const msg_address_t recipient,
    const uint8_t nak_code)
{
    struct msg_header_t hdr = {
        .sender = MSG_ADDRESS_LPC1114,
        .recipient = recipient,
        .payload_length = 0,
        .flags = MSG_FLAG_NAK | (nak_code & 0xF)
        };
    return comm_tx_message(&hdr, NULL, 0);
}

enum msg_status_t comm_tx_message(
    const struct msg_header_t *hdr,
    const uint8_t *payload,
    const msg_checksum_t checksum)
{
    switch (hdr->recipient) {
    case MSG_ADDRESS_LPC1114:
    {
        return MSG_INVALID_ADDRESS;
    }
    case MSG_ADDRESS_HOST:
    {
        return comm_tx_message_uart(hdr, payload, checksum);
    }
    default:
        return MSG_INVALID_ADDRESS;
    }
}
