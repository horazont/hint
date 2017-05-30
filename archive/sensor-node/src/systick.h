#ifndef SYSTICK_H
#define SYSTICK_H

#include "config.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

typedef uint16_t systick_t;

extern volatile systick_t tick;

void systick_init();

static inline systick_t systick_add_to_now(const systick_t ticks_to_add)
{
    return tick + ticks_to_add;
}

void systick_wait_until(const systick_t ticks);

static inline void systick_wait_for(const systick_t ticks)
{
    if (ticks < 2) {
        // we use delay_ms here, to avoid any races.
        _delay_ms(ticks);
        return;
    }

    systick_wait_until(systick_add_to_now(ticks));
}

#endif
