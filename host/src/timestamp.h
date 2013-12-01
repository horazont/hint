#ifndef _TIMESTAMP_H
#define _TIMESTAMP_H

#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

static inline bool timestamp_less(
    const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec < b->tv_sec) {
        return true;
    } else if (a->tv_sec == b->tv_sec) {
        return a->tv_nsec < b->tv_nsec;
    } else {
        return false;
    }
}

static inline void timestamp_print(
    const struct timespec *a)
{
    fprintf(stderr, "tv_sec=%ld; tv_nsec=%ld;\n", a->tv_sec, a->tv_nsec);
}

void timestamp_add_msec(
    struct timespec *to, uint32_t msec);

uint32_t timedelta_in_msec(
    const struct timespec *a, const struct timespec *b);

#endif

