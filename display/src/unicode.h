#ifndef _UTF8_H
#define _UTF8_H

#include <stdint.h>

#define CODEPOINT_REPLACEMENT_CHARACTER (0xfffd)

typedef uint32_t codepoint_t;

typedef uint8_t *utf8_str_t;
typedef const uint8_t *utf8_ctx_t;

void utf8_init(utf8_ctx_t *ctx, const utf8_str_t str);
codepoint_t utf8_next(utf8_ctx_t *ctx);

#endif
