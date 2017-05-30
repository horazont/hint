#include "uart.h"

inline uint8_t uart_rx_sync()
{
    while (!(UCSRA & (1<<RXC)));
    uint8_t buf = (uint8_t)UDR;
    return buf;
}

inline void uart_tx_sync(char chr)
{
    while (!(UCSRA & (1<<UDRE)));
    UDR = chr;
}

void uart_tx_buf_sync(const char *src, int len)
{
    const char *end = &src[len];
    while (src != end) {
        uart_tx_sync(*src++);
    }
}
