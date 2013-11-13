#include "time.h"

#include "core/systick/systick.h"

struct ticks_t ticks_get()
{
    struct ticks_t result = {
        .rollovers = systickGetRollovers(),
        .ticks = systickGetTicks()
    };
    return result;
}
