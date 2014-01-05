#ifndef _SCREEN_MISC_H
#define _SCREEN_MISC_H

#include "screen.h"

struct screen_misc_t {

};

void screen_misc_free(struct screen_t *screen);
void screen_misc_hide(struct screen_t *screen);
void screen_misc_init(struct screen_t *screen);
void screen_misc_repaint(struct screen_t *screen);
void screen_misc_show(struct screen_t *screen);

#endif
