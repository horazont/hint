#include "unicode.h"

void utf8_init(utf8_ctx_t *ctx, utf8_cstr_t str)
{
    *ctx = str;
}

codepoint_t utf8_next(utf8_ctx_t *ctx)
{
    if (!ctx || !*ctx || !**ctx) {
        return 0;
    }

    const uint8_t *str = *ctx;
    switch (*str & 0xC0) {
    case 0x00:
    case 0x40:
    {
        ++*ctx;
        return *str;
    }
    case 0x80:
    {
        ++*ctx;
        return CODEPOINT_REPLACEMENT_CHARACTER;
    }
    case 0xC0:
    default:
    {
        break;
    }
    }

    uint8_t orig_startbyte = *str;
    uint8_t startbyte = *str << 1;
    int bytecount = 0;
    codepoint_t result = 0;
    uint8_t mask = 0x3f;

    while (startbyte & 0x80) {
        ++str;
        result <<= 6;
        result |= *str & 0x3f;
        startbyte <<= 1;
        bytecount++;
        mask >>= 1;
    }

    result |= (orig_startbyte & mask) << (bytecount * 6);
    *ctx = ++str;
    return result;
}
