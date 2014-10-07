#include "systick.h"

#include <util/atomic.h>

#define sleep() __asm__ __volatile__("sleep":::"memory")

volatile systick_t tick;

void systick_init()
{
    tick = 0;
    ATOMIC_BLOCK(ATOMIC_FORCEON)
    {
        // tick is at every ms
        OCR1AH = ((8000 >> 8) & 0xFF);
        OCR1AL = (8000 & 0xFF);
        TCCR1A = 0;
        TCCR1B = (1<<CS10) | (1<<WGM12);
        TIMSK |= (1<<OCIE1A);
    }
}

static inline void systick_wait_wraparound()
{
    const systick_t prev = tick;
    while (tick == prev) {
        sleep();
    }
    while (tick > prev) {
        sleep();
    }
}

void systick_wait_until(const systick_t until)
{
    // we will have to wait for a wraparound
    if (until < tick) {
        systick_wait_wraparound();
    }

    while (tick < until) {
        sleep();
    }
}

ISR(TIMER1_COMPA_vect)
{
    tick += 1;
}
