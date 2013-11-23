#ifndef _TIMESTAMP_H
#define _TIMESTAMP_H

#include <time.h>
#include <stdint.h>

uint32_t timedelta_in_msec(
    const struct timespec *a, const struct timespec *b);

#endif

