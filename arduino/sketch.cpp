#include <Arduino.h>

#include <Wire.h>
#include <OneWire.h>

#include "common/comm.h"
#include "common/comm_arduino.h"
#include "common/comm_lpc1114.h"

#undef LED_BUILTIN
#define LED_BUILTIN (2)

TwoWire i2c;
OneWire ds(4);

// family code: 1 byte
// serial number: 6 bytes
// temperature reading: 2 bytes
// padding: 2 bytes
// total: 11 bytes payload + 4 bytes header + 1 byte checksum = 16 bytes

struct msg_t msg_skeleton = {
    .header = HDR_INIT(MSG_ADDRESS_ARDUINO,
                       MSG_ADDRESS_HOST,
                       11UL,
                       0UL),
};

struct ard_msg_t &msg_payload = *reinterpret_cast<struct ard_msg_t*>(&msg_skeleton.data[0]);

const struct msg_encoded_header_t encoded_header = raw_to_wire(
    &msg_skeleton.header);

static_assert(sizeof(int) == 2, "");
static_assert(sizeof(long int) == 4, "bar");

void blink_code(int code) {
    for (int i = 1; i <= code; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(500);
        digitalWrite(LED_BUILTIN, LOW);
        delay(500);
    }
}

void setup() {
    i2c.begin(ARD_I2C_ADDRESS);

    pinMode(LED_BUILTIN, OUTPUT);

    delay(1000);
}

static inline void read_sensor_and_send_readout(uint8_t addr[8])
{
    // check the family code for details
    if ((addr[0] != 0x28) && (addr[0] != 0x22)) {
        blink_code(2);
        return;
    }
    blink_code(1);

    uint8_t data[12];
    int16_t raw_temperature;

    ds.reset();
    ds.select(addr);
    ds.write(0x44, 1);
    delay(1000);

    ds.reset();
    ds.select(addr);
    ds.write(0xBE);

    for (int i = 0; i < 9; i++) {
        data[i] = ds.read();
    }

    raw_temperature = (data[1] << 8) | data[0];

    switch (data[4] & 0x60) {
    case 0x00:
    {
        // 9 bit resolution
        raw_temperature = raw_temperature & 0xFF8;
        break;
    }
    case 0x20:
    {
        // 10 bit resolution
        raw_temperature = raw_temperature & 0xFFC;
        break;
    }
    case 0x40:
    {
        // 11 bit resolution
        raw_temperature = raw_temperature & 0xFFE;
        break;
    }
    }

    msg_payload.subject = ARD_SUBJECT_SENSOR_READOUT;
    memcpy(&msg_payload.data.sensor_readout.sensor_id[0],
           &addr[0],
           7);
    msg_payload.data.sensor_readout.raw_readout = htole16(raw_temperature);
    msg_skeleton.checksum = checksum(msg_skeleton.data,
                                     HDR_GET_PAYLOAD_LENGTH(msg_skeleton.header));

    i2c.beginTransmission(LPC_I2C_ADDRESS);
    i2c.write((uint8_t*)&encoded_header, sizeof(struct msg_encoded_header_t));
    i2c.write(msg_skeleton.data, HDR_GET_PAYLOAD_LENGTH(msg_skeleton.header));
    i2c.write(&msg_skeleton.checksum, sizeof(msg_checksum_t));
    int result = i2c.endTransmission();
    if (result > 0) {
        blink_code(result);
    }
}

void loop() {
    // blink_code(1);
    uint8_t addr[8];
    memset(&addr[0], 0, sizeof(addr));
    while (ds.search(addr)) {
        if (OneWire::crc8(addr, 7) != addr[7]) {
            // crc error
            break;
        }

        read_sensor_and_send_readout(addr);

        delay(250);
    }
    // delay(10000);
    // delay(1000);
}
