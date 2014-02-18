#ifndef _COMMON_COMM_H
#define _COMMON_COMM_H

#include <stdint.h>
#include <stdbool.h>

#ifndef __LITTLE_ENDIAN
#include <endian.h>
#else
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 4321
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#endif

typedef uint8_t msg_checksum_t;
typedef uint8_t msg_address_t;
typedef uint16_t msg_length_t;

#define MSG_ADDRESS_HOST                    (0x0UL)
#define MSG_ADDRESS_LPC1114                 (0x1UL)
#define MSG_ADDRESS_ARDUINO                 (0x2UL)

//! acknowledgement of a previous message
#define MSG_FLAG_ACK                        (0x10)
//! command not allowed at this time
#define MSG_FLAG_NAK_CODE_ORDER             (0x20)
//! unknown command
#define MSG_FLAG_NAK_CODE_UNKNOWN_COMMAND   (0x40)
//! not enough memory to perform the given operation
#define MSG_FLAG_NAK_OUT_OF_MEMORY          (0x60)
//! payload_length must be zero; send reply to the sender with
//! MSG_FLAG_ACK set in addition to MSG_FLAG_ECHO
#define MSG_FLAG_ECHO                       (0x80)
//! this is a bitmask which forces reset of all communication structures
//! this includes resetting any buffers
#define MSG_FLAG_RESET                      (0xFF)

#define MSG_MASK_FLAG_BITS                  (0xF0)

struct msg_header_t {
    uint32_t data;
};

struct msg_encoded_header_t {
    uint32_t encoded_data;
};

#define MSG_HDR_MASK_FLAGS                  (0xFF000000)
#define MSG_HDR_SHIFT_FLAGS                 (24)
#define MSG_HDR_MASK_PAYLOAD_LENGTH         (0x00FF0000)
#define MSG_HDR_SHIFT_PAYLOAD_LENGTH        (16)
#define MSG_HDR_MASK_SENDER                 (0x00003000)
#define MSG_HDR_SHIFT_SENDER                (12)
#define MSG_HDR_MASK_RECIPIENT              (0x00000300)
#define MSG_HDR_SHIFT_RECIPIENT             (8)
#define MSG_HDR_MASK_RESERVED               (0x000000FF)
#define MSG_HDR_SHIFT_RESERVED              (0)

#define HDR_GET_(hdr, field) (((hdr).data & MSG_HDR_MASK_##field) >> MSG_HDR_SHIFT_##field)
#define HDR_SET_(hdr, value, field) {(hdr).data = (((hdr).data & ~MSG_HDR_MASK_##field) | (((value) << MSG_HDR_SHIFT_##field) & MSG_HDR_MASK_##field));}


#define HDR_GET_FLAGS(hdr) HDR_GET_(hdr, FLAGS)
#define HDR_GET_PAYLOAD_LENGTH(hdr) HDR_GET_(hdr, PAYLOAD_LENGTH)
#define HDR_GET_SENDER(hdr) HDR_GET_(hdr, SENDER)
#define HDR_GET_RECIPIENT(hdr) HDR_GET_(hdr, RECIPIENT)
#define HDR_GET_RESERVED(hdr) HDR_GET_(hdr, RESERVED)

#define HDR_SET_FLAGS(hdr, value) HDR_SET_(hdr, value, FLAGS)
#define HDR_SET_PAYLOAD_LENGTH(hdr, value) HDR_SET_(hdr, value, PAYLOAD_LENGTH)
#define HDR_SET_SENDER(hdr, value) HDR_SET_(hdr, value, SENDER)
#define HDR_SET_RECIPIENT(hdr, value) HDR_SET_(hdr, value, RECIPIENT)
#define HDR_SET_RESERVED(hdr, value) HDR_SET_(hdr, value, RESERVED)

#define HDR_SET_MESSAGE_ID(hdr, id) HDR_SET_FLAGS(hdr, (id)&0xF)
#define HDR_SET_ECHO_ID(hdr, id) HDR_SET_FLAGS(hdr, MSG_FLAG_ECHO | (id&0xF))

#define HDR_COMPOSE(sender, recipient, payload_length, flags) ( \
    (((sender) << MSG_HDR_SHIFT_SENDER) & MSG_HDR_MASK_SENDER) | \
    (((recipient) << MSG_HDR_SHIFT_RECIPIENT) & MSG_HDR_MASK_RECIPIENT) | \
    (((payload_length) << MSG_HDR_SHIFT_PAYLOAD_LENGTH) & MSG_HDR_MASK_PAYLOAD_LENGTH) | \
    (((flags) << MSG_HDR_SHIFT_FLAGS) & MSG_HDR_MASK_FLAGS))
#define HDR_INIT(sender, recipient, payload_length, flags) \
    {HDR_COMPOSE(sender, recipient, payload_length, flags)}
#define HDR_INIT_EX(sender, recipient, payload_length, flags, reserved) \
    {HDR_COMPOSE(sender, recipient, payload_length, flags) | ((reserved) & 0xFF)}
#define HDR_SET(hdrvar, sender, recipient, payload_length, flags) (hdrvar).data = HDR_COMPOSE(sender, recipient, payload_length, flags)

enum msg_status_t {
    MSG_NO_ERROR = 0,
    MSG_TIMEOUT,
    MSG_CHECKSUM_ERROR,
    MSG_TOO_LONG,
    MSG_INVALID_ADDRESS
};

#define MSG_MAX_PAYLOAD     (0xfa)
#define MSG_MAX_ADDRESS     (0x3)
#define MSG_MAX_LENGTH      (MSG_MAX_PAYLOAD + sizeof(struct msg_header_t) + sizeof(msg_checksum_t))

#define MSG_UART_BAUDRATE   (115200)

struct msg_t {
    struct msg_header_t header;
    uint8_t data[MSG_MAX_PAYLOAD];
    msg_checksum_t checksum;
};

struct msg_buffer_t {
    bool in_use;
    struct msg_t msg;
};

#if __STDC_VERSION__ >= 201112L

_Static_assert(sizeof(struct msg_header_t) == sizeof(uint32_t),
               "Header isn't properly packed");

#endif

#define CHECKSUM_PRIME (13)

#define CHECKSUM_CTX(prefix) \
    uint16_t prefix##_A = 1; \
    uint16_t prefix##_B = 0;

#define CHECKSUM_CLEAR(prefix) \
    prefix##_A = 1; \
    prefix##_B = 0;

#define CHECKSUM_PUSH(prefix, value) do { \
    prefix##_A = (prefix##_A + value) % CHECKSUM_PRIME; \
    prefix##_B = (prefix##_A + prefix##_B) % CHECKSUM_PRIME; \
    } while (0)

#define CHECKSUM_FINALIZE(prefix) (prefix##_A << 4) | (prefix##_B)

static inline msg_checksum_t checksum(const uint8_t *buffer, const msg_length_t len)
{
    CHECKSUM_CTX(adler);
    const uint8_t *end = buffer + len;
    while (buffer != end) {
        CHECKSUM_PUSH(adler, *buffer++);
    }
    return CHECKSUM_FINALIZE(adler);
}

#if BYTE_ORDER == BIG_ENDIAN

static inline struct msg_encoded_header_t raw_to_wire(const struct msg_header_t *raw)
{
    struct msg_encoded_header_t encoded = {htole32(raw->data)};
    return encoded;
}

static inline struct msg_header_t wire_to_raw(const struct msg_encoded_header_t *encoded)
{
    struct msg_header_t raw = {le32toh(encoded->encoded_data)};
    return raw;
}

#else

static inline struct msg_encoded_header_t raw_to_wire(const struct msg_header_t *raw)
{
    struct msg_encoded_header_t encoded = {raw->data};
    return encoded;
}

static inline struct msg_header_t wire_to_raw(const struct msg_encoded_header_t *encoded)
{
    struct msg_header_t raw = {encoded->encoded_data};
    return raw;
}

#endif

#endif
