#include "comm.h"

#include "time.h"
#include "config.h"

static uint32_t timeout VAR_RAM = 100;
// static enum msg_status_t read_status VAR_RAM = MSG_NO_ERROR;

void comm_init(const uint32_t baudrate)
{
    NVIC_DisableIRQ(UART_IRQn);

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
    UART_U0IER = UART_U0IER_RBR_Interrupt_Enabled | UART_U0IER_RLS_Interrupt_Enabled;

    NVIC_EnableIRQ(UART_IRQn);
}

//~ uint16_t msg_receive_buffer(void *dest, const uint16_t len)
//~ {
    //~ struct timestamp_t last_byte = ticks_get();
    //~ uint16_t offs = 0;
    //~ while (offs != len) {
        //~ while (!uartRxBufferDataPending()) {
            //~ struct timestamp_t curr_time = ticks_get();
            //~ if (ticks_delta(&curr_time, &last_byte) > timeout) {
                //~ read_status = MSG_TIMEOUT;
                //~ return offs;
            //~ }
        //~ }
        //~ while (uartRxBufferDataPending()) {
            //~ (*uint8_t)(dest)[offs++] = uartRxBufferRead();
        //~ }
    //~ }
    //~ read_status = MSG_NO_ERROR;
    //~ return offs;
//~ }

uint32_t msg_get_timeout()
{
    return timeout;
}

//~ enum msg_status_t msg_receive_header(struct msg_header_t *dest)
//~ {
    //~ if (msg_receive_buffer(dest, sizeof(struct msg_header_t)) < sizeof(struct msg_header_t))
    //~ {
        //~ return read_status;
    //~ }
    //~ return MSG_NO_ERROR;
//~ }
//~
//~ enum msg_status_t msg_receive_payload(uint8_t *buffer, uint16_t len)
//~ {
    //~ msg_checksum_t checksum_provided;
    //~ if (msg_receive_buffer_adler32(buffer, len) < len)
    //~ {
        //~ return read_status;
    //~ }
    //~ if (msg_receive_buffer(&checksum_provided, sizeof(msg_checksum_t)) < sizeof(msg_checksum_t))
    //~ {
        //~ return read_status;
    //~ }
    //~ msg_checksum_t checksum_received = checksum(buffer, len);
    //~ if (checksum_received == checksum_provided) {
        //~ return MSG_NO_ERROR;
    //~ } else {
        //~ return MSG_CHECKSUM_ERROR;
    //~ }
//~ }

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
//~
//~ enum msg_status_t msg_tx(
    //~ const addressee_t addressee,
    //~ const uint8_t *payload,
    //~ const uint16_t payload_length)
//~ {
    //~ if (payload_length > MSG_MAX_PAYLOAD) {
        //~ return MSG_TOO_LONG;
    //~ }
    //~ if (addressee > MSG_MAX_ADDRESSEE) {
        //~ return MSG_INVALID_ADDRESSEE;
    //~ }
//~
    //~ {
        //~ struct msg_header_t header;
        //~ header.payload_length = payload_length;
        //~ header.addressee = addressee;
        //~ uartSend(&header, sizeof(struct msg_header_t));
    //~ }
//~
    //~ const uint8_t *curr = payload;
    //~ const uint8_t *end = payload + payload_length;
    //~ CHECKSUM_CTX(adler);
    //~ while (curr != end) {
        //~ uint8_t byte = *curr++;
        //~ uartSendByte(byte);
        //~ CHECKSUM_PUSH(adler, byte);
    //~ }
    //~ msg_checksum_t checksum = CHECKSUM_FINALIZE(adler);
    //~ uartSendByte(checksum);
//~ }

void msg_set_timeout(const uint32_t ms)
{
    timeout = ms;
}

typedef enum {
    IRQS_IDLE,
    IRQS_RECEIVE_HEADER,
    IRQS_ROUTING,
    IRQS_RECEIVING
} irq_state_t;

typedef void (*fwd_func_t)(const uint8_t byte);

static irq_state_t state VAR_RAM = IRQS_IDLE;
static struct msg_header_t current_header VAR_RAM = {
    .payload_length = 0,
    .sender = MSG_ADDRESS_LPC1114,
    .recipient = MSG_ADDRESS_LPC1114};
static bool buffer_busy VAR_RAM = false;
static uint8_t buffer[MSG_MAX_PAYLOAD] VAR_RAM;
static uint8_t *recv_dest VAR_RAM = 0;
static uint8_t *recv_end VAR_RAM = 0;
static msg_length_t routing_remaining VAR_RAM = 0;
static fwd_func_t router VAR_RAM = 0;
static uint32_t received_irqn VAR_RAM = EINT0_IRQn;

void UART_IRQHandler(void)
{
    switch (state) {
    case IRQS_IDLE:
    {
        state = IRQS_RECEIVE_HEADER;
        recv_dest = (uint8_t*)(&current_header);
        recv_end = (uint8_t*)(&current_header) + sizeof(struct msg_header_t);
        // intentional: receive the first bytes immediately
    }
    case IRQS_RECEIVE_HEADER:
    {
        do {
            uint32_t status = UART_U0LSR;
            if ((status & UART_U0LSR_RDR_MASK) != UART_U0LSR_RDR_DATA) {
                break;
            }
            *recv_dest++ = UART_U0RBR;
        } while (recv_dest != recv_end);
        if (recv_dest != recv_end) {
            return;
        }
        switch (current_header.recipient) {
        case MSG_ADDRESS_LPC1114:
        {
            if (buffer_busy) {
                // TODO: reply with busy error and eat all teh bytes
            }
            recv_dest = &buffer[0];
            recv_end = &buffer[current_header.payload_length];
            state = IRQS_RECEIVING;
            UART_IRQHandler();
            return;
        }
        case MSG_ADDRESS_HOST:
        {
            state = IRQS_ROUTING;
            router = &msg_router_uart;
            routing_remaining = current_header.payload_length+sizeof(msg_checksum_t);
            msg_tx_via_uart(&current_header, sizeof(struct msg_header_t));
            UART_IRQHandler();
            return;
        }
        case MSG_ADDRESS_ARDUINO:
        {
            state = IRQS_ROUTING;
            router = &msg_router_spi;
            routing_remaining = current_header.payload_length;
            // TODO: send header via SPI
            UART_IRQHandler();
            return;
        }
        default:
        {
            // fixme: properly return an error here. lets just enter
            // invalid state for now :/
        }
        }
        break;
    }
    case IRQS_ROUTING:
    {
        do {
            uint32_t status = UART_U0LSR;
            if ((status & UART_U0LSR_RDR_MASK) != UART_U0LSR_RDR_DATA) {
                break;
            }
            router(UART_U0RBR);
            routing_remaining--;
        } while (routing_remaining);
        if (routing_remaining == 0) {
            state = IRQS_IDLE;
        }
        break;
    }
    case IRQS_RECEIVING:
    {
        do {
            uint32_t status = UART_U0LSR;
            if ((status & UART_U0LSR_RDR_MASK) != UART_U0LSR_RDR_DATA) {
                break;
            }
            uint8_t byte = UART_U0RBR;
            UART_U0THR = byte;
            *recv_dest++ = byte;
        } while (recv_dest != recv_end);
        if (recv_dest == recv_end) {
            state = IRQS_IDLE;
        }
        NVIC_SetPendingIRQ(received_irqn);
        break;
    }
    default:
    {
        // invalid state
    }
    }
}
