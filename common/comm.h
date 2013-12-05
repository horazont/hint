#ifndef _COMMON_COMM_H
#define _COMMON_COMM_H

#include <stdint.h>
#include <stdbool.h>

#ifndef __LITTLE_ENDIAN
#include <endian.h>
#endif

typedef uint8_t msg_checksum_t;
typedef uint8_t msg_address_t;
typedef uint16_t msg_length_t;

#define MSG_ADDRESS_HOST                    (0x0)
#define MSG_ADDRESS_LPC1114                 (0x1)
#define MSG_ADDRESS_ARDUINO                 (0x2)

#define MSG_FLAG_ACK                        (0x10)
#define MSG_FLAG_NAK                        (0x20)

//! command not allowed at this time
#define MSG_NAK_CODE_ORDER                  (0x01)
//! unknown command
#define MSG_NAK_CODE_UNKNOWN_COMMAND        (0x02)
//! not enough memory to perform the given operation
#define MSG_NAK_OUT_OF_MEMORY               (0x03)

struct msg_header_t {
    uint32_t data;
};

#define MSG_HDR_MASK_FLAGS                  (0xFF000000)
#define MSG_HDR_SHIFT_FLAGS                 (24)
#define MSG_HDR_MASK_PAYLOAD_LENGTH         (0x00FF0000)
#define MSG_HDR_SHIFT_PAYLOAD_LENGTH        (16)
#define MSG_HDR_MASK_SENDER                 (0x00003000)
#define MSG_HDR_SHIFT_SENDER                (12)
#define MSG_HDR_MASK_RECIPIENT              (0x00000300)
#define MSG_HDR_SHIFT_RECIPIENT             (8)

#define HDR_GET_(hdr, field) (((hdr).data & MSG_HDR_MASK_##field) >> MSG_HDR_SHIFT_##field)
#define HDR_SET_(hdr, value, field) {(hdr).data = (((hdr).data & ~MSG_HDR_MASK_##field) | ((value << MSG_HDR_SHIFT_##field) & MSG_HDR_MASK_##field));}


#define HDR_GET_FLAGS(hdr) HDR_GET_(hdr, FLAGS)
#define HDR_GET_PAYLOAD_LENGTH(hdr) HDR_GET_(hdr, PAYLOAD_LENGTH)
#define HDR_GET_SENDER(hdr) HDR_GET_(hdr, SENDER)
#define HDR_GET_RECIPIENT(hdr) HDR_GET_(hdr, RECIPIENT)

#define HDR_SET_FLAGS(hdr, value) HDR_SET_(hdr, value, FLAGS)
#define HDR_SET_PAYLOAD_LENGTH(hdr, value) HDR_SET_(hdr, value, PAYLOAD_LENGTH)
#define HDR_SET_SENDER(hdr, value) HDR_SET_(hdr, value, SENDER)
#define HDR_SET_RECIPIENT(hdr, value) HDR_SET_(hdr, value, RECIPIENT)

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

_Static_assert(sizeof(struct msg_header_t) == sizeof(uint32_t),
               "Header isn't properly packed");

#define CHECKSUM_PRIME (13)

#define CHECKSUM_CTX(prefix) \
    uint16_t prefix##_A = 1; \
    uint16_t prefix##_B = 0;

#define CHECKSUM_PUSH(prefix, value) do { \
    prefix##_A = (prefix##_A + value) % CHECKSUM_PRIME; \
    prefix##_B = (prefix##_B + prefix##_B) % CHECKSUM_PRIME; \
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

#define header_to_wire(hdrptr) (hdrptr)->data = htole32((hdrptr)->data)
#define wire_to_header(hdrptr) (hdrptr)->data = le32toh((hdrptr)->data)

#endif
