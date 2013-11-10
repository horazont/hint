#ifndef _TIME_H
#define _TIME_H

#include <stdint.h>

struct ticks_t {
    uint32_t rollovers;
    uint32_t ticks;
};

struct ticks_t ticks_get();

static inline uint32_t ticks_delta(
    const struct ticks_t *a,
    const struct ticks_t *b)
{
    switch (b->rollovers - a->rollovers) {
    case 0:
    {
        return b->ticks - a->ticks;
    }
    case 1:
    {
        if (a->ticks > b->ticks) {
            return a->ticks - b->ticks;
        }
        return 0xffffffff;
    }
    default:
        return 0xffffffff;
    }
}

#endif
