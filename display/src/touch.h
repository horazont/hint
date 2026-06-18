#ifndef _TOUCH_H
#define _TOUCH_H

#include <stdint.h>
#include <stdbool.h>

#include "draw.h"
#include "fp11_4.h"

/* the code in touch.h and touch.c is largely taken from the original
 * firmware code, as no specification for the touchpad could be found.
 */

#define TOUCH_MIN_PRESSURE 200

struct touch_calibration_t {
    /* offset_{x,y} is in ±11.4 format, scale_{x,y} is in ±0.15
     * format */

    fp11_4_t offset_x;
    int16_t scale_x;
    fp11_4_t offset_y;
    int16_t scale_y;
};

enum touch_intr_state_t {
    TOUCH_STATE_IDLE,
    TOUCH_STATE_SAMPLING_Z1,
    TOUCH_STATE_SAMPLING_Z2,
    TOUCH_STATE_SAMPLING_X,
    TOUCH_STATE_SAMPLING_Y
};

extern volatile bool touch_pending;
extern volatile enum touch_intr_state_t touch_intr_state;

int touch_intr_start();
void touch_intr_sm();

void touch_init();
void touch_calculate_calibration(
    const struct point11_4_t *lcd1,
    const struct point11_4_t *lcd2,
    const struct point11_4_t *touch1,
    const struct point11_4_t *touch2,
    int merge);
void touch_get_calibration(struct touch_calibration_t *dest);
coord_int_t touch_get_x();
coord_int_t touch_get_y();
coord_int_t touch_get_z();
coord_int_t touch_get_raw_x();
coord_int_t touch_get_raw_y();
coord_int_t touch_get_raw_z();
void touch_sample();
void touch_test();
void touch_wait_for_raw(coord_int_t *x, coord_int_t *y, coord_int_t *z);
void touch_wait_for_clear();

#endif
