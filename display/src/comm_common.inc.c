/* data */

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

/* code: buffer and queue management */

static inline void swap_app_buffers()
{
    volatile struct msg_buffer_t *tmp = appbuffer_front;
    appbuffer_front = appbuffer_back;
    appbuffer_back = tmp;
}

static inline bool comm_enqueue_tx_nowait(
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

/* code: utilities */

static inline void copy_header(volatile struct msg_header_t *dst, const volatile struct msg_header_t *src)
{
    memcpy((void*)dst, (const void*)src, sizeof(struct msg_header_t));
}
