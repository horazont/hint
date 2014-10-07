#include "bcd.h"

bool bcd_inc2(bcd2_t *dest)
{
    ++(*dest);
    if ((*dest & 0xF) >= 0xA) {
        *dest += 0x10;
        if (*dest >= 0xA0) {
            *dest = 0;
            return true;
        } else {
            *dest &= 0xF0;
        }
    }
    return false;
}

char* bcd_to_digits2(bcd2_t src, bool fill)
{
    static char buf[2];
    buf[1] = '0' + (src & 0xF);
    src >>= 4;
    if (fill || src > 0) {
        buf[0] = '0' + (src & 0xF);
    } else {
        buf[0] = ' ';
    }
    return buf;
}
