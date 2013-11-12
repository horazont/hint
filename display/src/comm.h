#ifndef _HOSTCOM_H
#define _HOSTCOM_H

#include "common/comm.h"

#define COMM_ERR_NONE 0
#define COMM_ERR_NO_BACKBUFFER_AVAILABLE 1
#define COMM_ERR_NO_ROUTEBUFFER_AVAILABLE 2
#define COMM_ERR_UNKNOWN_RECIPIENT 3

void comm_init(const uint32_t baudrate);

/**
 * Handle routing and reception of messages.
 */
void UART_IRQHandler(void);

volatile struct msg_buffer_t *volatile appbuffer_front;
volatile struct msg_buffer_t *volatile appbuffer_back;

#endif
