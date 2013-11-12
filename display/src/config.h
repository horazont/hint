#ifndef _CONFIG_H
#define _CONFIG_H

#include "lpc111x.h"
#include <stdint.h>

#define CFG_CPU_CCLK            (48000000L)
#define CFG_UART_BAUDRATE       (115200)
#define CFG_UART_BUFSIZE        (128)
#define CFG_SYSTICK_DELAY_IN_MS (1)

#define VAR_RAM __attribute__((section(".data")))
#define VAR_FLASH __attribute__((section(".text#")))
#define VAR_RAM_ZERO __attribute__((section(".bss")))

#endif
