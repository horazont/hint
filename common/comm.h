#ifndef _COMMON_COMM_H
#define _COMMON_COMM_H

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t msg_checksum_t;
typedef uint8_t msg_address_t;
typedef uint16_t msg_length_t;

#define MSG_ADDRESS_HOST    (0x0)
#define MSG_ADDRESS_LPC1114 (0x1)
#define MSG_ADDRESS_ARDUINO (0x2)

struct msg_header_t {
    uint16_t reserved0      : 6;
    uint16_t payload_length : 10;
    uint16_t reserved1      : 2;
    uint16_t sender         : 2;
    uint16_t reserved2      : 2;
    uint16_t recipient      : 2;
    uint16_t reserved3      : 8;
};

enum msg_status_t {
    MSG_NO_ERROR = 0,
    MSG_TIMEOUT,
    MSG_CHECKSUM_ERROR,
    MSG_TOO_LONG,
    MSG_INVALID_ADDRESS
};

#define MSG_MAX_PAYLOAD     (0x3ff)
#define MSG_MAX_ADDRESS     (0x3)
#define MSG_MAX_LENGTH      (MSG_MAX_PAYLOAD + sizeof(struct msg_header_t) + sizeof(msg_checksum_t))

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

msg_checksum_t checksum(const uint8_t *buffer, const msg_length_t len);

#define CHECKSUM_PRIME (13)

#define CHECKSUM_CTX(prefix) \
    uint16_t prefix##_A; \
    uint16_t prefix##_B;

#define CHECKSUM_PUSH(prefix, value) do { \
    prefix##_A = (prefix##_A + value) % CHECKSUM_PRIME; \
    prefix##_B = (prefix##_B + prefix##_B) % CHECKSUM_PRIME; \
    } while (0)

#define CHECKSUM_FINALIZE(prefix) (prefix##_A << 4) | (prefix##_B)

#endif
