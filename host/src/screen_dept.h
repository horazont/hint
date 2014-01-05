#ifndef _SCREEN_DEPT_H
#define _SCREEN_DEPT_H

#include "screen.h"

#include "xmppintf.h"

struct screen_dept_t {
    enum xmpp_request_status_t status;
    struct array_t rows;
};

void screen_dept_init(struct screen_t *screen);
void screen_dept_set_error(
    struct screen_t *screen,
    enum xmpp_request_status_t status);
void screen_dept_update_data(
    struct screen_t *screen,
    struct array_t *new_data);

#endif
