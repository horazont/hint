#include <Arduino.h>

#include <Wire.h>

#include "common/comm.h"
#include "common/comm_arduino.h"
#include "common/comm_lpc1114.h"

#undef LED_BUILTIN
#define LED_BUILTIN (2)

TwoWire i2c;

struct msg_t msg = {
    .header = HDR_INIT(MSG_ADDRESS_ARDUINO,
                       MSG_ADDRESS_HOST,
                       16UL,
                       0UL),
};

static_assert(sizeof(int) == 2, "");
static_assert(sizeof(long int) == 4, "bar");

struct msg_encoded_header_t encoded_header;

void blink_code(int code) {
    for (int i = 1; i <= code; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(500);
        digitalWrite(LED_BUILTIN, LOW);
        delay(500);
    }
}

void setup() {
    delay(200);
    i2c.begin(ARD_I2C_ADDRESS);
    uint8_t v = 0x00;
    for (msg_length_t i = 0; i < HDR_GET_PAYLOAD_LENGTH(msg.header); i++) {
        msg.data[i] = v;
        v += 0x11;
    }
    msg.checksum = checksum(msg.data, HDR_GET_PAYLOAD_LENGTH(msg.header));
    encoded_header = raw_to_wire(&msg.header);

    pinMode(LED_BUILTIN, OUTPUT);
    delay(10);
    blink_code(2);
}

void loop() {
    delay(2000);
    i2c.beginTransmission(LPC_I2C_ADDRESS);
    i2c.write((uint8_t*)&encoded_header, sizeof(struct msg_encoded_header_t));
    i2c.write(msg.data, HDR_GET_PAYLOAD_LENGTH(msg.header));
    i2c.write(&msg.checksum, sizeof(msg_checksum_t));

    int result = i2c.endTransmission();
    blink_code(result+1);
}
