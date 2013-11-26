#ifndef _UTF8_H
#define _UTF8_H

#include <stdint.h>

#define CODEPOINT_REPLACEMENT_CHARACTER (0xfffd)
#define CODEPOINT_ELLIPSIS (0x2026)

typedef uint32_t codepoint_t;

typedef uint8_t *utf8_str_t;
typedef const uint8_t *utf8_cstr_t;
typedef utf8_cstr_t utf8_ctx_t;

/**
 * Initialize a context for decoding of an utf-8 string.
 *
 * @param ctx context to initialize
 * @param str string to associate with the context
 */
void utf8_init(utf8_ctx_t *ctx, utf8_cstr_t str);

/**
 * Decode and return the next utf-8 byte sequence.
 *
 * @param ctx context to operate with
 * @return codepoint obtained from decoding the utf-8 code
 */
codepoint_t utf8_next(utf8_ctx_t *ctx);

/**
 * Return the pointer to the location of the first byte of the next
 * character in the given utf-8 context.
 *
 * @param ctx context to operate with
 * @return pointer to the current character
 */
utf8_cstr_t utf8_get_ptr(const utf8_ctx_t *ctx);

#endif
