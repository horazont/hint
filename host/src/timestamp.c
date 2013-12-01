#include "timestamp.h"

void timestamp_add_msec(
    struct timespec *to, uint32_t msec)
{
    to->tv_sec += msec/1000;
    to->tv_nsec += msec%1000;
    if (to->tv_nsec > 1000000000) {
        to->tv_sec += 1;
        to->tv_nsec -= 1000000000;
    }
}

uint32_t timedelta_in_msec(
    const struct timespec *a, const struct timespec *b)
{
    if (timestamp_less(a, b)) {
        return 0;
    }
    if ((a->tv_sec - b->tv_sec) >= (UINT32_MAX/1000)) {
        return UINT32_MAX;
    }

    uint32_t result = (a->tv_sec - b->tv_sec) * 1000;
    if (result == 0) {
        result += (a->tv_nsec - b->tv_nsec) / 1000000;
    } else {
        result += a->tv_nsec / 1000000;
        result += (1000 - b->tv_nsec / 1000000);
        result -= 1000;
    }

    return result;
}
