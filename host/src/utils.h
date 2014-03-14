#ifndef _UTILS_H
#define _UTILS_H

#include <time.h>

#define CELSIUS_OFFSET (273.15f);
#define ISODATE_LENGTH 20
typedef char isodate_buffer[ISODATE_LENGTH+1];
const char *isodate_fmt;

static inline float kelvin_to_celsius(const float value)
{
    return value - CELSIUS_OFFSET;
}

void format_isodate(isodate_buffer buffer, const struct tm *time);
void panicf(const char *format, ...) __attribute__((noreturn));
char recv_char(int fd);
void send_char(int fd, const char chr);

#endif
