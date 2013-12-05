#include "timestamp.h"

#include <errno.h>
#include <string.h>
#include <assert.h>

#include "utils.h"

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW (4)
#endif

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

int32_t timestamp_delta_in_msec(
    const struct timespec *a, const struct timespec *b)
{
    if (timestamp_less(a, b)) {
        return -timestamp_delta_in_msec(b, a);
    }
    if ((a->tv_sec - b->tv_sec) >= (INT32_MAX/1000)) {
        return INT32_MAX;
    }

    int32_t result = (a->tv_sec - b->tv_sec) * 1000;
    if (result == 0) {
        result += (a->tv_nsec - b->tv_nsec) / 1000000;
    } else {
        result += a->tv_nsec / 1000000;
        result += (1000 - b->tv_nsec / 1000000);
        result -= 1000;
    }

    return result;
}

void timestamp_sanity_check()
{
    struct timespec t;
    int result = clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    if (result != 0) {
        panicf("timestamp: sanity check failed: "
               "clock_gettime(CLOCK_MONOTONIC_RAW, &t) does not "
               "work: %d (%s)\n",
               errno, strerror(errno));
    }
}

void timestamp_gettime(struct timespec *t)
{
    int result = clock_gettime(CLOCK_MONOTONIC_RAW, t);
    assert(result == 0);
}

