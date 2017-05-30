#ifndef _COMMON_ARDUINO_H
#define _COMMON_ARDUINO_H

#define ARD_SUBJECT_SENSOR_READOUT (1)

struct __attribute__((packed)) ard_ev_sensor_readout_t {
    // 1 byte family code, 6 bytes serial number
    uint8_t sensor_id[7];
    int16_t raw_readout;
};

struct __attribute__((packed)) ard_msg_t {
    uint8_t subject;
    union {
        struct ard_ev_sensor_readout_t sensor_readout;
        // 27 is the maximum payload size we can achieve with the standard
        // arduino i2c library, need to subtract one uint8_t
        uint8_t raw[26];
    } data;
};

#define ARD_I2C_ADDRESS (0x44)

#endif
