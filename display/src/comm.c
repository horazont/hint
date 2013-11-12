#include "comm.h"

#include "time.h"
#include "config.h"

#include <string.h>

#define MSG_QUEUE_SIZE 2

static uint32_t timeout VAR_RAM = 100;
// static enum msg_status_t read_status VAR_RAM = MSG_NO_ERROR;

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
        msg_length_t remaining;
        volatile struct msg_buffer_t *dest_msg;
    } state;

    struct msg_buffer_t route_buffer;

    struct port_queue_item_t queue[MSG_QUEUE_SIZE];
    int active_queue;
};

typedef enum {
    RX_IDLE,
    RX_RECEIVE_HEADER,
    RX_RECEIVE_PAYLOAD,
    RX_RECEIVE_CHECKSUM,
    RX_DUMP
} uart_rx_state_t;

typedef enum {
    TX_IDLE,
    TX_SEND_HEADER,
    TX_SEND_PAYLOAD,
    TX_SEND_CHECKSUM
} uart_tx_state_t;

static uart_rx_state_t uart_rx_state VAR_RAM = RX_IDLE;
static uart_tx_state_t uart_tx_state VAR_RAM = TX_IDLE;
static struct comm_port_t uart VAR_RAM = {
    .state = {
        .curr_header = {
            .payload_length = 0,
            .sender = MSG_ADDRESS_LPC1114,
            .recipient = MSG_ADDRESS_LPC1114
        },
        .recv_dest = 0,
        .recv_end = 0,
        .trns_src = 0,
        .trns_end = 0,
        .remaining = 0,
        .dest_msg = NULL
    },
    .queue = {
        {
            .empty = true
        },
        {
            .empty = true
        }
    },
    .route_buffer = {
        .in_use = false
    },
    .active_queue = 0
};

// frontbuffer is locked until the user releases it
static volatile bool frontbuffer_locked VAR_RAM = false;
// if the backbuffer has been filled while the frontbuffer is locked,
// this is true
static volatile bool backbuffer_ready VAR_RAM = false;
static volatile struct msg_buffer_t appbuffer[2] VAR_RAM = {
    {
        .in_use = false
    },
    {
        .in_use = false
    }
};
volatile struct msg_buffer_t *volatile appbuffer_front VAR_RAM = &appbuffer[0];
volatile struct msg_buffer_t *volatile appbuffer_back VAR_RAM = &appbuffer[1];

static uint32_t received_irqn VAR_RAM = EINT0_IRQn;

void comm_init(const uint32_t baudrate)
{
    NVIC_DisableIRQ(UART_IRQn);

    for (int i = 0; i < 2; i++) {
        appbuffer[i].in_use = false;
    }
    uart.route_buffer.in_use = false;
    frontbuffer_locked = false;

    /* Set 1.6 UART RXD */
    IOCON_PIO1_6 &= ~IOCON_PIO1_6_FUNC_MASK;
    IOCON_PIO1_6 |= IOCON_PIO1_6_FUNC_UART_RXD;

    /* Set 1.7 UART TXD */
    IOCON_PIO1_7 &= ~IOCON_PIO1_7_FUNC_MASK;
    IOCON_PIO1_7 |= IOCON_PIO1_7_FUNC_UART_TXD;

    /* Enable UART clock */
    SCB_SYSAHBCLKCTRL |= (SCB_SYSAHBCLKCTRL_UART);
    SCB_UARTCLKDIV = SCB_UARTCLKDIV_DIV1;     /* divided by 1 */

    /* 8 bits, no Parity, 1 Stop bit */
    UART_U0LCR = (UART_U0LCR_Word_Length_Select_8Chars |
                  UART_U0LCR_Stop_Bit_Select_1Bits |
                  UART_U0LCR_Parity_Disabled |
                  UART_U0LCR_Parity_Select_OddParity |
                  UART_U0LCR_Break_Control_Disabled |
                  UART_U0LCR_Divisor_Latch_Access_Enabled);

    /* Baud rate */
    uint32_t reg_val = SCB_UARTCLKDIV;
    uint32_t fDiv = (((CFG_CPU_CCLK * SCB_SYSAHBCLKDIV)/reg_val)/16)/baudrate;

    UART_U0DLM = fDiv / 256;
    UART_U0DLL = fDiv % 256;

    /* Set DLAB back to 0 */
    UART_U0LCR = (UART_U0LCR_Word_Length_Select_8Chars |
                UART_U0LCR_Stop_Bit_Select_1Bits |
                UART_U0LCR_Parity_Disabled |
                UART_U0LCR_Parity_Select_OddParity |
                UART_U0LCR_Break_Control_Disabled |
                UART_U0LCR_Divisor_Latch_Access_Disabled);

    /* Enable and reset TX and RX FIFO. */
    UART_U0FCR = (UART_U0FCR_FIFO_Enabled |
                  UART_U0FCR_Rx_FIFO_Reset |
                  UART_U0FCR_Tx_FIFO_Reset);

    /* Read to clear the line status. */
    reg_val = UART_U0LSR;

    /* Enable the UART Interrupt */
    NVIC_EnableIRQ(UART_IRQn);
    UART_U0IER = UART_U0IER_RBR_Interrupt_Enabled;

    NVIC_EnableIRQ(UART_IRQn);
}

uint32_t msg_get_timeout()
{
    return timeout;
}

void msg_router_uart(const uint8_t byte)
{
    // FIXME: this does not care at all about overruns :/
    UART_U0THR = byte;
}

void msg_router_spi(const uint8_t byte)
{
    byte ^ byte;
}

void msg_tx_via_uart(void *buffer, uint16_t len)
{
    uint8_t *curr = buffer;
    uint8_t *end = curr+len;
    while (curr != end) {
        msg_router_uart(*curr++);
    }
}

void msg_set_timeout(const uint32_t ms)
{
    timeout = ms;
}

bool comm_enqueue_tx_nowait(
    struct comm_port_t *port,
    const volatile struct msg_header_t *header,
    const volatile uint8_t *data,
    const volatile msg_checksum_t checksum,
    volatile bool *flag_to_clear)
{
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        if (port->queue[i].empty) {
            port->queue[i].empty = false;
            port->queue[i].flag_to_clear = flag_to_clear;
            port->queue[i].header = header;
            port->queue[i].data = data;
            port->queue[i].checksum = checksum;
            return true;
        }
    }
    return false;
}

void comm_enqueue_tx_wait(
    struct comm_port_t *port,
    const struct msg_header_t *header,
    const uint8_t *data,
    const msg_checksum_t checksum,
    bool *flag_to_clear)
{
    while (!comm_enqueue_tx_nowait(port, header, data, checksum, flag_to_clear))
    {
    }
}

/**
 * Receive until either recv_dest has reached it's end or no more data
 * is available.
 *
 * @return true if the buffer has reached it's end.
 */
static inline bool uart_rx_recv()
{
    while (uart.state.recv_dest != uart.state.recv_end) {
        uint32_t status = UART_U0LSR;
        if ((status & UART_U0LSR_RDR_MASK) != UART_U0LSR_RDR_DATA) {
            return false;
        }
        *uart.state.recv_dest++ = UART_U0RBR;
    }
    return true;
}

static inline void swap_app_buffers()
{
    volatile struct msg_buffer_t *tmp = appbuffer_front;
    appbuffer_front = appbuffer_back;
    appbuffer_back = tmp;
}

static inline void copy_header(volatile struct msg_header_t *dst, const volatile struct msg_header_t *src)
{
    _Static_assert(sizeof(struct msg_header_t) == 4, "Rewrite header copy");
    // *(uint32_t*)dst = *(uint32_t*)src;
    memcpy((void*)dst, (const void*)src, sizeof(struct msg_header_t));
}

static inline bool uart_tx_trns()
{
    UART_U0THR = *uart.state.trns_src++;
    return uart.state.trns_src == uart.state.trns_end;
}

static inline void uart_tx_irq()
{
    switch (uart_tx_state) {
    case TX_IDLE:
    {
        for (uint_fast8_t i = 0; i < MSG_QUEUE_SIZE; i++) {
            if (!uart.queue[i].empty) {
                uart.active_queue = i;
                break;
            }
        }
        if (uart.active_queue == -1) {
            UART_U0IER &= ~(UART_U0IER_THRE_Interrupt_Enabled);
            return;
        }
        uart_tx_state = TX_SEND_HEADER;
        uart.state.trns_src = (const volatile uint8_t*)uart.queue[uart.active_queue].header;
        uart.state.trns_end = uart.state.trns_src + sizeof(struct msg_header_t);
    }
    case TX_SEND_HEADER:
    {
        if (!uart_tx_trns()) {
            return;
        }
        uart_tx_state = TX_SEND_PAYLOAD;
        uart.state.trns_src = uart.queue[uart.active_queue].data;
        uart.state.trns_end = uart.state.trns_src + uart.queue[uart.active_queue].header->payload_length;
        break;
    }
    case TX_SEND_PAYLOAD:
    {
        if (!uart_tx_trns()) {
            return;
        }
        uart_tx_state = TX_SEND_CHECKSUM;
        uart.state.trns_src = (const uint8_t*)&uart.queue[uart.active_queue].checksum;
        uart.state.trns_end = uart.state.trns_src + sizeof(msg_checksum_t);
        break;
    }
    case TX_SEND_CHECKSUM:
    {
        if (!uart_tx_trns()) {
            return;
        }
        uart_tx_state = TX_IDLE;
        if (uart.queue[uart.active_queue].flag_to_clear) {
            *uart.queue[uart.active_queue].flag_to_clear = false;
        }
        uart.queue[uart.active_queue].empty = true;
        uart.active_queue = -1;
        break;
    }
    }
}

void comm_trigger_tx_uart()
{
    if (uart_tx_state == TX_IDLE) {
        UART_U0IER |= UART_U0IER_THRE_Interrupt_Enabled;
        if (UART_U0LSR & UART_U0LSR_THRE) {
            uart_tx_irq();
        }
    }
}

static inline void uart_rx_irq()
{
    switch (uart_rx_state) {
    case RX_IDLE:
    {
        uart_rx_state = RX_RECEIVE_HEADER;
        uart.state.recv_dest = (uint8_t*)(&uart.state.curr_header);
        uart.state.recv_end = (uint8_t*)(&uart.state.curr_header) + sizeof(struct msg_header_t);
        // missing break is intentional: receive the first bytes immediately
    }
    case RX_RECEIVE_HEADER:
    {
        if (!uart_rx_recv()) {
            return;
        }

        switch (uart.state.curr_header.recipient) {
        case MSG_ADDRESS_LPC1114:
        {
            // this is me! either forward to local buffer or discard.
            if (appbuffer_back->in_use) {
                //~ dropped_message = true;
                //~ problem = COMM_ERR_NO_BACKBUFFER_AVAILABLE;
                uart_rx_state = RX_DUMP;
                uart.state.remaining = uart.state.curr_header.payload_length+sizeof(msg_checksum_t);
                return;
            }
            appbuffer_back->in_use = true;

            uart.state.dest_msg = appbuffer_back;
            break;
        }
        case MSG_ADDRESS_ARDUINO:
        case MSG_ADDRESS_HOST:
        {
            // use our routing buffer, if it's not blocked currently
            if (uart.route_buffer.in_use) {
                //~ dropped_message = true;
                //~ problem = COMM_ERR_NO_ROUTEBUFFER_AVAILABLE;
                uart_rx_state = RX_DUMP;
                uart.state.remaining = uart.state.curr_header.payload_length+sizeof(msg_checksum_t);
                return;
            }

            uart.state.dest_msg = &uart.route_buffer;
            break;
        }
        default:
        {
            // discard, we have no idea where to forward to
            //~ dropped_message = true;
            //~ problem = COMM_ERR_UNKNOWN_RECIPIENT;
            uart_rx_state = RX_DUMP;
            uart.state.remaining = uart.state.curr_header.payload_length+sizeof(msg_checksum_t);
            return;
        }
        }

        uart_rx_state = RX_RECEIVE_PAYLOAD;
        uart.state.recv_dest = &uart.state.dest_msg->msg.data[0];
        uart.state.recv_end = uart.state.recv_dest + uart.state.curr_header.payload_length;
        // we can smoothly continue here if more data is available
    }
    case RX_RECEIVE_PAYLOAD:
    {
        if (!uart_rx_recv()) {
            return;
        }
        uart_rx_state = RX_RECEIVE_CHECKSUM;
        uart.state.recv_dest = (uint8_t*)&uart.state.dest_msg->msg.checksum;
        uart.state.recv_end = uart.state.recv_dest + sizeof(msg_checksum_t);
    }
    case RX_RECEIVE_CHECKSUM:
    {
        if (!uart_rx_recv()) {
            return;
        }
        uart_rx_state = RX_IDLE;
        copy_header(&uart.state.dest_msg->msg.header,
                    &uart.state.curr_header);
        switch (uart.state.curr_header.recipient) {
        case MSG_ADDRESS_LPC1114:
        {
            if (!frontbuffer_locked) {
                swap_app_buffers();
                frontbuffer_locked = true;
            } else {
                backbuffer_ready = true;
            }
            NVIC_SetPendingIRQ(received_irqn);
            break;
        }
        case MSG_ADDRESS_HOST:
        {
            if (!comm_enqueue_tx_nowait(
                    &uart,
                    &uart.state.dest_msg->msg.header,
                    &uart.state.dest_msg->msg.data[0],
                    uart.state.dest_msg->msg.checksum,
                    &uart.state.dest_msg->in_use))
            {
                uart.state.dest_msg->in_use = false;
            } else {
                comm_trigger_tx_uart();
            }
            break;
        }
        }
        break;
    }
    case RX_DUMP:
    {
        while (uart.state.remaining) {
            uint32_t status = UART_U0LSR;
            if ((status & UART_U0LSR_RDR_MASK) != UART_U0LSR_RDR_DATA) {
                return;
            }
            UART_U0SCR = UART_U0RBR;
            uart.state.remaining--;
        }
        uart_rx_state = RX_IDLE;
        break;
    }
    }
}

void UART_IRQHandler(void)
{
    uint32_t iir = UART_U0IIR;
    switch (iir & (0x7<<1)) {
    case (0x3<<1):
    case (0x2<<1):
    case (0x6<<1):
    {
        // UART rx event
        uart_rx_irq();
        break;
    }
    case (0x1<<1):
    {
        // tx buffer empty
        uart_tx_irq();
        break;
    }
    }
}
