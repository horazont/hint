#ifndef UART_H
#define UART_H

#include "config.h"

#include <stdint.h>

#include <avr/io.h>

uint8_t uart_rx_sync();
void uart_tx_sync(char byte);
void uart_tx_buf_sync(const char *src, int len);

#endif
