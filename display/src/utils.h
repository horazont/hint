#ifndef _UTILS_H
#define _UTILS_H

#include <stdint.h>

#include "systick/systick.h"

#define NOP()                          __asm__ volatile ("nop")
#define ENABLE_IRQ()                   __asm__ volatile ("cpsie i")
#define DISABLE_IRQ()                  __asm__ volatile ("cpsid i")

#define ADC_AD0DRn(chn) (*pREG32(ADC_AD0DR0 + (chn*4)))
#define ADC_READY(chn) (ADC_AD0DRn(chn) & (1<<31))
#define ADC_EXTRACT(chn) ((ADC_AD0DRn(chn)>>6) & 0x3FF)
#define ADC_READ(chn, x)             { \
    ADC_AD0CR |= (1<<chn) | ADC_AD0CR_START_STARTNOW; \
    /* wait until conversion finishes (bit 31 set) */ \
    while (!ADC_READY(chn));                          \
    /* disable conversion, disable all channels */ \
    ADC_AD0CR &= 0xF8FFFF00; \
    x = ADC_EXTRACT(chn);}

inline void delay_ms(uint16_t ms)
{
    systickDelay(ms / CFG_SYSTICK_DELAY_IN_MS);
}

#endif
