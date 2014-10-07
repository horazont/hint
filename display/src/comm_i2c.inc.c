typedef enum {
    RXI_IDLE,
    RXI_RECEIVE_HEADER,
    RXI_RECEIVE_PAYLOAD,
    RXI_RECEIVE_CHECKSUM
} i2c_rx_state_t;

static i2c_rx_state_t i2c_rx_state VAR_RAM = RXI_IDLE;
volatile uint32_t last_i2c_state = 0x00;

static struct {
    struct {
        struct msg_header_t curr_header;
        volatile uint8_t slave_address;
        volatile uint8_t *buffer_dest;
        volatile uint8_t *buffer_end;
        uint16_t recv_checksum_A;
        uint16_t recv_checksum_B;
        msg_length_t remaining;
        volatile struct msg_buffer_t *dest_msg;
    } state;

    struct msg_buffer_t route_buffer;

    struct port_queue_item_t queue[MSG_QUEUE_SIZE];
    int active_queue;

} i2c VAR_RAM = {
    .state = {
        .slave_address = 0x00,
        .buffer_dest = 0,
        .buffer_end = 0,
        .remaining = 0,
        .dest_msg = 0
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
    .active_queue = -1
};

static inline void i2c_init(uint32_t i2c_bitrate)
{
    uint32_t sclhl = CFG_CPU_CCLK / i2c_bitrate / 2;

    NVIC_DisableIRQ(I2C_IRQn);

    IOCON_PIO0_5 = IOCON_PIO0_5_FUNC_I2CSDA | IOCON_PIO0_5_I2CMODE_STANDARDI2C;
    IOCON_PIO0_4 = IOCON_PIO0_4_FUNC_I2CSCL | IOCON_PIO0_4_I2CMODE_STANDARDI2C;

    I2C_I2CCONCLR = I2C_I2CCONCLR_AAC
                  | I2C_I2CCONCLR_SIC
                  | I2C_I2CCONCLR_STAC
                  | I2C_I2CCONCLR_I2ENC;

    I2C_I2CADR0 = (LPC_I2C_ADDRESS << 1) & 0xFF; // default address
    I2C_I2CADR1 = 0x00; // ignore
    I2C_I2CADR2 = 0x00; // ignore

    I2C_I2CSCLH = sclhl;
    I2C_I2CSCLL = sclhl;

    NVIC_EnableIRQ(I2C_IRQn);

    last_i2c_state = 0xff;

    I2C_I2CCONSET = I2C_I2CCONSET_I2EN | I2C_I2CCONSET_AA;
}

static inline bool i2c_rx_recv()
{
    *i2c.state.buffer_dest++ = I2C_I2CDAT;
    return (i2c.state.buffer_dest == i2c.state.buffer_end);
}

static inline bool i2c_rx_recv_chksummed()
{
    uint8_t data = I2C_I2CDAT;
    CHECKSUM_PUSH(i2c.state.recv_checksum, data);
    *i2c.state.buffer_dest++ = data;
    return (i2c.state.buffer_dest == i2c.state.buffer_end);
}

static inline void i2c_nak()
{
    I2C_I2CCONCLR = I2C_I2CCONCLR_SIC | I2C_I2CCONCLR_AAC;
}

static inline void i2c_ack()
{
    I2C_I2CCONSET = I2C_I2CCONSET_AA;
    I2C_I2CCONCLR = I2C_I2CCONCLR_SIC;
}

void I2C_IRQHandler()
{
    uint8_t state = I2C_I2CSTAT;
    last_i2c_state = (last_i2c_state << 8) & 0xFFFFFF00;
    last_i2c_state |= state;
    switch (state) {
    case 0x60: // Slave RX: Slave Address + Write received, ACK returned
    case 0x68: // Slave RX: Arbitration lost as master, now addressed for write
    case 0x70: // Slave RX: General call received, ACK returned
    case 0x78: // Slave RX: Arbitration lost as master, general call received
    {
        // FIXME: 0x68 should probably take care of buffering

        // acknowledge transfer
        I2C_I2CCONSET = I2C_I2CCONSET_AA;

        i2c_rx_state = RXI_RECEIVE_HEADER;
        i2c.state.buffer_dest = (uint8_t*)(&i2c.state.curr_header);
        i2c.state.buffer_end = i2c.state.buffer_dest + sizeof(struct msg_header_t);

        I2C_I2CCONCLR = I2C_I2CCONCLR_SIC;

        break;
    }
    case 0x80: // Slave RX: Directly addressed, Data received, ACK returned
    case 0x90: // Slave RX: General call, Data received, ACK returned
    {
        switch (i2c_rx_state) {
        case RXI_IDLE:
        {
            i2c_nak();
            return;
        }
        case RXI_RECEIVE_HEADER:
        {
            if (!i2c_rx_recv()) {
                i2c_ack();
                return;
            }

            switch (HDR_GET_RECIPIENT(i2c.state.curr_header)) {
            case MSG_ADDRESS_LPC1114:
            {
                i2c_nak();
                return;
            }
            case MSG_ADDRESS_HOST:
            case MSG_ADDRESS_ARDUINO:
            {
                if (i2c.route_buffer.in_use) {
                    i2c_nak();
                    return;
                }

                i2c.state.dest_msg = &i2c.route_buffer;
                break;
            }
            default:
            {
                i2c_nak();
                return;
            }
            }

            CHECKSUM_CLEAR(i2c.state.recv_checksum);
            i2c_rx_state = RXI_RECEIVE_PAYLOAD;
            i2c.state.buffer_dest = &i2c.state.dest_msg->msg.data[0];
            i2c.state.buffer_end = i2c.state.buffer_dest + HDR_GET_PAYLOAD_LENGTH(i2c.state.curr_header);
            i2c_ack();
            last_i2c_state = (last_i2c_state << 8) | 0x11;
            return;
        }
        case RXI_RECEIVE_PAYLOAD:
        {
            if (!i2c_rx_recv_chksummed()) {
                i2c_ack();
                return;
            }
            last_i2c_state = (last_i2c_state << 8) | 0xCC;
            i2c_rx_state = RXI_RECEIVE_CHECKSUM;
            i2c.state.buffer_dest = &i2c.state.dest_msg->msg.checksum;
            i2c.state.buffer_end = i2c.state.buffer_dest + sizeof(msg_checksum_t);
            i2c_ack();
            return;
        }
        case RXI_RECEIVE_CHECKSUM:
        {
            if (!i2c_rx_recv()) {
                i2c_ack();
                return;
            }
            // End of transmission
            i2c_nak();
            // marker for debug
            last_i2c_state = (last_i2c_state << 8) | 0xFF;
            uint8_t checksum = CHECKSUM_FINALIZE(i2c.state.recv_checksum);
            if (checksum != i2c.state.dest_msg->msg.checksum) {
                // FIXME: discard
            }

            i2c_rx_state = RXI_IDLE;
            copy_header(&i2c.state.dest_msg->msg.header,
                        &i2c.state.curr_header);

            switch (HDR_GET_RECIPIENT(i2c.state.curr_header)) {
            case MSG_ADDRESS_LPC1114:
            {
                if (!frontbuffer_locked) {
                    swap_app_buffers();
                    frontbuffer_locked = true;
                } else {
                    backbuffer_ready = true;
                }
                NVIC_SetPendingIRQ(received_irqn);
            }
            case MSG_ADDRESS_HOST:
            {
                if (!comm_enqueue_tx_nowait(
                        &uart.queue,
                        &i2c.state.dest_msg->msg.header,
                        &i2c.state.dest_msg->msg.data[0],
                        i2c.state.dest_msg->msg.checksum,
                        &i2c.state.dest_msg->in_use))
                {
                    // routing failed
                    i2c.state.dest_msg->in_use = false;
                } else {
                    uart_tx_trigger();
                }
                break;
            }
            case MSG_ADDRESS_ARDUINO:
            {
                // route to self.
                break;
            }
            }

        }
        }
        break;
    }

    case 0xA0: // Slave RX: Unexpected end of data
    {
        // this is EOT right now ...
        i2c_ack();
        i2c_rx_state = RXI_IDLE;
        break;
    }

    case 0x88: // Slave RX: Directly addressed, Data received, NAK returned
    case 0x98: // Slave RX: General call, Data received, NAK returned
    {
        i2c_ack();
        break;
    }

    case 0xA8: // Slave TX: Slave Address + Read received, ACK returned
    case 0xB0: // Slave TX: Arbitration lost as master, now addressed for read
    case 0xB8: // Slave TX: Data transmitted, ACK received
    case 0xC0: // Slave TX: Data transmitted, NAK received
    case 0xC8: // Slave TX: End of data, ACK received



    // unhandled codes
    case 0x00: // Bus Error
    {
        I2C_I2CCONSET |= I2C_I2CCONSET_STO | I2C_I2CCONSET_AA;
        I2C_I2CCONCLR |= I2C_I2CCONCLR_SIC;
        break;
    }
    case 0x08: // Master: START transmitted
    case 0x10: // Master: ReSTART transmitted
    case 0x18: // Master TX: Slave Address + Write transmitted, ACK received
    case 0x20: // Master TX: Slave Address + Write transmitted, NAK received
    case 0x28: // Master TX: Data transmitted, ACK received
    case 0x30: // Master TX: Data transmitted, NAK received
    case 0x38: // Master TX: Arbitration lost, restart will happen when bus is free
    case 0x40: // Master RX: Slave Address + Read transmitted, ACK received
    case 0x48: // Master RX: Slave Address + Read transmitted, NAK received
    case 0x50: // Master RX: Data transmitted, ACK received
    case 0x58: // Master RX: Data transmitted, NAK received
    default: // unexpected!
    {
        i2c_nak();
        break;
    }
    }
}
