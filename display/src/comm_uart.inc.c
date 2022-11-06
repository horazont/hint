/* types */

typedef enum {
    TXU_IDLE,
    TXU_SEND_HEADER,
    TXU_SEND_PSEUDOHEADER,
    TXU_SEND_PAYLOAD,
    TXU_SEND_CHECKSUM
} uart_tx_state_t;

/* data */

volatile uart_rx_state_t uart_rx_state VAR_RAM = RXU_IDLE;
static uart_tx_state_t uart_tx_state VAR_RAM = TXU_IDLE;
static struct comm_port_t uart VAR_RAM = {
    .state = {
        .curr_header = {0},
        .recv_dest = 0,
        .recv_end = 0,
        .trns_src = 0,
        .trns_end = 0,
        .remaining = 0,
        .dest_msg = NULL
    },
    .queue = {
        .items = {
            {
                .empty = true
            },
            {
                .empty = true
            }
        },
        .active_item = -1
    },
    .route_buffer = {
        .in_use = false
    },
};

static uint8_t pending_pings VAR_RAM = 0;
static struct msg_header_t ping_header VAR_RAM =
    HDR_INIT(
        MSG_ADDRESS_LPC1114,
        MSG_ADDRESS_HOST,
        0,
        MSG_FLAG_ACK | MSG_FLAG_ECHO);
static volatile uint32_t buffer = 0;

static inline void uart_init(const uint32_t baudrate)
{
    NVIC_DisableIRQ(UART_IRQn);

    buffer = 0xdeadbeef;

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

/* code: interrupt tx subroutines */

static inline bool uart_tx_trns()
{
    UART_U0THR = *uart.state.trns_src++;
    return uart.state.trns_src == uart.state.trns_end;
}

void uart_tx_irq()
{
    switch (uart_tx_state) {
    case TXU_IDLE:
    {
        if (pending_pings > 0) {
            pending_pings -= 1;
            uart_tx_state = TXU_SEND_PSEUDOHEADER;
            uart.state.trns_src = (const volatile uint8_t*)(&ping_header);
            uart.state.trns_end = uart.state.trns_src + sizeof(struct msg_header_t);
            // this is more than ugly (we just proceed to
            // TXU_SEND_HEADER). but we can do that because only one
            // byte gets transmitted per uart_tx_trns() call.
            uart_tx_irq();
            break;
        } else {
            for (uint_fast8_t i = 0; i < MSG_QUEUE_SIZE; i++) {
                if (!uart.queue.items[i].empty) {
                    uart.queue.active_item = i;
                    break;
                }
            }
            if (uart.queue.active_item == -1) {
                UART_U0IER &= ~(UART_U0IER_THRE_Interrupt_Enabled);
                return;
            }
            uart_tx_state = TXU_SEND_HEADER;
            uart.state.trns_src = (const volatile uint8_t*)uart.queue.items[uart.queue.active_item].header;
            uart.state.trns_end = uart.state.trns_src + sizeof(struct msg_header_t);
        }
        __attribute__((fallthrough));
    }
    case TXU_SEND_HEADER:
    {
        if (!uart_tx_trns()) {
            return;
        }
        uint16_t len = HDR_GET_PAYLOAD_LENGTH(*uart.queue.items[uart.queue.active_item].header);
        if (len == 0) {
            // allow tx-ing payloadless messages
            uart_tx_state = TXU_IDLE;
            if (uart.queue.items[uart.queue.active_item].flag_to_clear) {
                *uart.queue.items[uart.queue.active_item].flag_to_clear = false;
            }
            uart.queue.items[uart.queue.active_item].empty = true;
            uart.queue.active_item = -1;
            break;
        }
        uart_tx_state = TXU_SEND_PAYLOAD;
        uart.state.trns_src = uart.queue.items[uart.queue.active_item].data;
        uart.state.trns_end = uart.state.trns_src + len;
        break;
    }
    case TXU_SEND_PSEUDOHEADER:
    {
        if (!uart_tx_trns()) {
            return;
        }
        uart_tx_state = TXU_IDLE;
        // pseudoheader packets are not associated to any buffer, nor
        // do they have a payload. so just reset to TXU_IDLE.
        break;
    }
    case TXU_SEND_PAYLOAD:
    {
        if (!uart_tx_trns()) {
            return;
        }
        uart_tx_state = TXU_SEND_CHECKSUM;
        uart.state.trns_src = (const uint8_t*)&uart.queue.items[uart.queue.active_item].checksum;
        uart.state.trns_end = uart.state.trns_src + sizeof(msg_checksum_t);
        break;
    }
    case TXU_SEND_CHECKSUM:
    {
        if (!uart_tx_trns()) {
            return;
        }
        uart_tx_state = TXU_IDLE;
        if (uart.queue.items[uart.queue.active_item].flag_to_clear) {
            *uart.queue.items[uart.queue.active_item].flag_to_clear = false;
        }
        uart.queue.items[uart.queue.active_item].empty = true;
        uart.queue.active_item = -1;
        break;
    }
    }
}

static inline void uart_tx_trigger()
{
    // only trigger if uart tx is in IDLE state; otherwise, it'll
    // retrigger automatically
    if (uart_tx_state == TXU_IDLE) {
        UART_U0IER |= UART_U0IER_THRE_Interrupt_Enabled;
        if (UART_U0LSR & UART_U0LSR_THRE) {
            uart_tx_irq();
        }
    }
}

/* code: interrupt rx subroutines */

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

static inline bool uart_rx_recv_chksummed()
{
    while (uart.state.recv_dest != uart.state.recv_end) {
        uint32_t status = UART_U0LSR;
        if ((status & UART_U0LSR_RDR_MASK) != UART_U0LSR_RDR_DATA) {
            return false;
        }
        uint8_t byte = UART_U0RBR;
        *uart.state.recv_dest++ = byte;
        CHECKSUM_PUSH(uart.state.recv_checksum, byte);
    }
    return true;
}

static inline void uart_rx_irq_end_of_transmission()
{
    switch (uart_rx_state) {
    case RXU_IDLE:
    {
        // okay
        break;
    }
    case RXU_DUMP:
    {
        // just stop dumping
        uart_rx_state = RXU_IDLE;
        break;
    }
    case RXU_RECEIVE_CHECKSUM:
    case RXU_RECEIVE_HEADER:
    case RXU_RECEIVE_PAYLOAD:
    {
        switch (HDR_GET_RECIPIENT(uart.state.curr_header)) {
        case MSG_ADDRESS_LPC1114:
        {
            // not routed, release buffer
            appbuffer_back->in_use = false;
            break;
        }
        default:
        {
            // routed message, release buffer
            uart.route_buffer.in_use = false;
            break;
        }
        }
        uart_rx_state = RXU_IDLE;
        break;
    }
    }
    // disable and reset the timer
    TMR_COMM_TIMEOUT_TCR = (0<<0) | (1<<1);
}

void uart_rx_irq()
{
    // on rx irq, we can reset the timer always
    TMR_COMM_TIMEOUT_TC = 0;
    switch (uart_rx_state) {
    case RXU_IDLE:
    {
        uart_rx_state = RXU_RECEIVE_HEADER;
        uart.state.recv_dest = (uint8_t*)(&uart.state.curr_header);
        uart.state.recv_end = (uint8_t*)(&uart.state.curr_header) + sizeof(struct msg_header_t);
        // missing break is intentional: receive the first bytes immediately
        // turn the timer on
        TMR_COMM_TIMEOUT_TCR = (1<<0) | (0<<1);
        __attribute__((fallthrough));
    }
    case RXU_RECEIVE_HEADER:
    {
        if (!uart_rx_recv()) {
            return;
        }

        switch (HDR_GET_RECIPIENT(uart.state.curr_header)) {
        case MSG_ADDRESS_LPC1114:
        {
            switch (HDR_GET_FLAGS(uart.state.curr_header) & MSG_MASK_FLAG_BITS) {
            case MSG_FLAG_ECHO:
            {
                // this is a ping, reply to it asap
                pending_pings += 1;
                uart_rx_state = RXU_IDLE;
                uart_tx_trigger();
                return;
            }
            case MSG_FLAG_RESET:
            {
                uart_rx_state = RXU_IDLE;
                appbuffer_back->in_use = false;
                backbuffer_ready = false;
                return;
            }
            }

            // this is me! either forward to local buffer or discard.
            if (appbuffer_back->in_use) {
                //~ dropped_message = true;
                //~ problem = COMM_ERR_NO_BACKBUFFER_AVAILABLE;
                uart_rx_state = RXU_DUMP;
                uart.state.remaining =
                    HDR_GET_PAYLOAD_LENGTH(uart.state.curr_header)
                    +sizeof(msg_checksum_t);
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
                uart_rx_state = RXU_DUMP;
                uart.state.remaining =
                    HDR_GET_PAYLOAD_LENGTH(uart.state.curr_header)
                    +sizeof(msg_checksum_t);
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
            uart_rx_state = RXU_DUMP;
            uart.state.remaining =
                HDR_GET_PAYLOAD_LENGTH(uart.state.curr_header)
                +sizeof(msg_checksum_t);
            return;
        }
        }

        CHECKSUM_CLEAR(uart.state.recv_checksum);
        uart_rx_state = RXU_RECEIVE_PAYLOAD;
        uart.state.recv_dest = &uart.state.dest_msg->msg.data[0];
        uart.state.recv_end = uart.state.recv_dest + HDR_GET_PAYLOAD_LENGTH(uart.state.curr_header);
        // we can smoothly continue here if more data is available
        __attribute__((fallthrough));
    }
    case RXU_RECEIVE_PAYLOAD:
    {
        if (!uart_rx_recv_chksummed()) {
            return;
        }
        uart_rx_state = RXU_RECEIVE_CHECKSUM;
        uart.state.recv_dest = (uint8_t*)&uart.state.dest_msg->msg.checksum;
        uart.state.recv_end = uart.state.recv_dest + sizeof(msg_checksum_t);
        __attribute__((fallthrough));
    }
    case RXU_RECEIVE_CHECKSUM:
    {
        if (!uart_rx_recv()) {
            return;
        }
        uint8_t checksum = CHECKSUM_FINALIZE(uart.state.recv_checksum);
        if (checksum != uart.state.dest_msg->msg.checksum) {
            uart_rx_irq_end_of_transmission();
            break;
        }
        uart_rx_state = RXU_IDLE;
        // reset the timer
        TMR_COMM_TIMEOUT_TCR = (0<<0);
        copy_header(&uart.state.dest_msg->msg.header,
                    &uart.state.curr_header);
        switch (HDR_GET_RECIPIENT(uart.state.curr_header)) {
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
                    &uart.queue,
                    &uart.state.dest_msg->msg.header,
                    &uart.state.dest_msg->msg.data[0],
                    uart.state.dest_msg->msg.checksum,
                    &uart.state.dest_msg->in_use))
            {
                uart.state.dest_msg->in_use = false;
            } else {
                uart_tx_trigger();
            }
            break;
        }
        }
        break;
    }
    case RXU_DUMP:
    {
        while (uart.state.remaining) {
            uint32_t status = UART_U0LSR;
            if ((status & UART_U0LSR_RDR_MASK) != UART_U0LSR_RDR_DATA) {
                return;
            }
            UART_U0SCR = UART_U0RBR;
            uart.state.remaining--;
        }
        uart_rx_state = RXU_IDLE;
        // disable and reset the timer
        TMR_COMM_TIMEOUT_TCR = (0<<0) | (1<<1);
        break;
    }
    }
}

/* code: interrupt handler */

void TIMER32_0_IRQHandler(void)
{
    TMR_COMM_TIMEOUT_IR = TMR_COMM_TIMEOUT_IR_RESET;
    uart_rx_irq_end_of_transmission();
}

void UART_IRQHandler(void)
{
    uint32_t iir = UART_U0IIR;
    switch (iir & (0x7<<1)) {
    case (0x3<<1):
    case (0x2<<1):
    {
        // UART rx event
        uart_rx_irq();
        break;
    }
    case (0x6<<1): // character timeout
    {
        // UART rx event
        uart_rx_irq();
        // abort any pending transmission
        uart_rx_irq_end_of_transmission();
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
