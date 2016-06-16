#ifndef _SCREEN_MISC_H
#define _SCREEN_MISC_H

#include "screen.h"

#include "xmppintf.h"

void screen_misc_free(struct screen_t *screen);
void screen_misc_hide(struct screen_t *screen);
void screen_misc_init(struct screen_t *screen, struct xmpp_t *xmpp);
void screen_misc_repaint(struct screen_t *screen);
void screen_misc_show(struct screen_t *screen);
void screen_misc_touch(struct screen_t *screen,
                       coord_int_t xc,
                       coord_int_t yc,
                       coord_int_t z);

#endif
