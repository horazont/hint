#ifndef BCD_H
#define BCD_H

#include "config.h"

#include <stdint.h>
#include <stdbool.h>

#define BCD2_DIGIT0 (0)
#define BCD2_DIGIT1 (4)

typedef uint8_t bcd2_t;
typedef uint16_t bcd4_t;

bool bcd_inc2(bcd2_t *dest);
char* bcd_to_digits2(bcd2_t src, bool fill);

#define bcd_set_digit2(value, digit, new_value) \
    (value) = ((value) & ~(0xF << digit)) | (new_value << digit)

#endif
