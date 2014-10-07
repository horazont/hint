#ifndef UART_ONEWIRE_H
#define UART_ONEWIRE_H

#include "config.h"

#include <stdbool.h>
#include <string.h>

#include "uart.h"

#define UART_1W_ERROR (0)
#define UART_1W_EMPTY (1)
#define UART_1W_PRESENCE (2)
#define UART_1W_NO_MORE_DEVICES (1)

#define UART_1W_ADDR_LEN (8)

typedef uint8_t onewire_addr_t[UART_1W_ADDR_LEN];

uint8_t onewire_findnext(onewire_addr_t addr);

static inline bool onewire_findfirst(onewire_addr_t dest)
{
    memset(&dest[0], UART_1W_ADDR_LEN, 0x00);
    return onewire_findnext(dest);
}

void onewire_init();
void onewire_ds18b20_broadcast_conversion();
void onewire_ds18b20_invoke_conversion(
    const onewire_addr_t device);
void onewire_ds18b20_read_scratchpad(
    const onewire_addr_t device,
    uint8_t blob[9]);
int16_t onewire_ds18b20_read_temperature(
    const onewire_addr_t device);

/**
 * Reset the one-wire bus. Return a value dependent on the state of the bus.
 *
 * @return @ref UART_1W_PRESENCE if the presence of a 1-wire device has been
 * detected. @ref UART_1W_EMPTY if no device was detected, but the read back
 * value was appropriate. @ref UART_1W_ERROR if the signal received on the bus
 * was malformed. This can happen on plugging/unplugging 1-wire devices using
 * parasite power.
 */
uint8_t onewire_reset();

#endif
