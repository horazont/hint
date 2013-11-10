#include "time.h"

#include "core/systick/systick.h"

struct timestamp_t ticks_get()
{
    struct timestamp_t result = {
        .rollovers = systickGetRollovers(),
        .ticks = systickGetTicks()
    };
    return result;
}
