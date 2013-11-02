#ifndef _UTILS_H
#define _UTILS_H

#include <stdint.h>

#include "projectconfig.h"

#include "systick/systick.h"

#define NOP()                          __asm__ volatile ("nop")
#define ENABLE_IRQ()                   __asm__ volatile ("cpsie i")
#define DISABLE_IRQ()                  __asm__ volatile ("cpsid i")


inline void delay_ms(uint16_t ms)
{
    systickDelay(ms / CFG_SYSTICK_DELAY_IN_MS);
}

#endif
