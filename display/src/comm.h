#ifndef _HOSTCOM_H
#define _HOSTCOM_H

#include "common/comm.h"

void comm_init(const uint32_t baudrate);

/**
 * Handle routing and reception of messages.
 */
void UART_IRQHandler(void);

#endif
