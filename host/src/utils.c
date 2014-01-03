#include "utils.h"

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

const char *isodate_fmt = "%Y-%m-%dT%H:%M:%SZ";

void format_isodate(isodate_buffer buffer, const struct tm *time)
{
    size_t result = strftime(buffer, ISODATE_LENGTH+1, isodate_fmt, time);
    assert(result == ISODATE_LENGTH);
}

void panicf(const char *format, ...)
{
    fprintf(stderr, "panic: ");
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    _exit(1);
}

char recv_char(int fd)
{
    char result;
    if (read(fd, &result, 1) != 1) {
        panicf("failed to recv pipechar: %d (%s)\n", errno, strerror(errno));
    }
    return result;
}

void send_char(int fd, const char chr)
{
    if (write(fd, &chr, 1) != 1) {
        panicf("failed to send pipechar: %d (%s)\n", errno, strerror(errno));
    };
}
