#ifndef _SCREEN_PIC_H
#define _SCREEN_PIC_H

#include "screen.h"

void screen_pic_free(struct screen_t *screen);
void screen_pic_hide(struct screen_t *screen);
void screen_pic_init(struct screen_t *screen);
void screen_pic_repaint(struct screen_t *screen);
void screen_pic_show(struct screen_t *screen);

#endif
