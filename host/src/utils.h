#ifndef _UTILS_H
#define _UTILS_H

void panicf(const char *format, ...) __attribute__((noreturn));
char recv_char(int fd);
void send_char(int fd, const char chr);

#endif
