#include "config.h"

#include <alloca.h>
#include <stdio.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <util/atomic.h>

#include "common/comm.h"
#include "common/comm_arduino.h"
#include "common/comm_lpc1114.h"

#include "uart_onewire.h"
#include "systick.h"

#include "USI_TWI_Master.h"

struct __attribute__((packed)) i2c_sensor_message
{
    uint8_t i2c_addr;
    struct msg_encoded_header_t header;
    uint8_t subject;
    union __attribute__((packed)) {
        struct __attribute__((packed)) {
            uint8_t sensor_addr[7];
            int16_t reading;
        } sensor_readout;
    } data;
    msg_checksum_t checksum;
};

static inline char nybble_to_hex(uint8_t nybble)
{
    if (nybble >= 0xA) {
        return (char)nybble + 55;
    } else {
        return (char)nybble + 48;
    }
}

static inline void uint8_to_hex(char buf[2], uint8_t value)
{
    char *dest = &buf[1];
    *dest-- = nybble_to_hex(value & 0xF);
    *dest = nybble_to_hex((value >> 4) & 0xF);
}

uint8_t send_readout(
    const uint8_t i2c_addr,
    const onewire_addr_t sensor,
    const int16_t reading)
{
    struct i2c_sensor_message msg = {
        .i2c_addr = (i2c_addr << 1),
        .header = HDR_INIT(MSG_ADDRESS_ARDUINO,
                           MSG_ADDRESS_HOST,
                           10UL,
                           0UL)
    };
    msg.subject = ARD_SUBJECT_SENSOR_READOUT;
    for (uint8_t i = 0; i < 7; i++) {
        msg.data.sensor_readout.sensor_addr[i] = sensor[i];
    }
    msg.data.sensor_readout.reading = reading;
    msg.checksum = checksum(&msg.subject,
                            sizeof(msg.subject) + sizeof(msg.data));

    if (!USI_TWI_Start_Transceiver_With_Data((uint8_t*)&msg, sizeof(msg)))
    {
        return 0x80 | USI_TWI_Get_State_Info();
    }

    return 0;
}

static void clear_addr(onewire_addr_t addr)
{
    for (uint_least8_t i = 0; i < UART_1W_ADDR_LEN; i++)
    {
        addr[i] = 0x00;
    }
}

static void strobe_led_500(uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        PORTD |= (1<<DDD2);
        _delay_ms(500);
        PORTD &= ~(1<<DDD2);
        _delay_ms(500);
    }
}

static void strobe_led_1000(uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        PORTD |= (1<<DDD2);
        _delay_ms(1000);
        PORTD &= ~(1<<DDD2);
        _delay_ms(1000);
    }
}

int main()
{
    DDRD = (1<<DDD2) | (1<<DDD3) | (1<<DDD4) | (1<<DDD5) | (1<<DDD6);
    DDRB = (1<<DDB3) | (1<<DDB2) | (1<<DDB4);

    _delay_ms(50);
    onewire_init();
    systick_init();
    USI_TWI_Master_Initialise();

    strobe_led_500(3);

    while (1) {
        strobe_led_1000(1);
        onewire_addr_t addr;
        clear_addr(addr);
        /* strobe_led_500(onewire_reset()+1); */
        // first command all sensors to do conversion
        onewire_ds18b20_broadcast_conversion();
        // now lets go over all of them and collect the data
        while (onewire_findnext(addr) == UART_1W_PRESENCE) {
            int16_t T;
            uint8_t status = onewire_ds18b20_read_temperature(addr, &T);
            if (status != UART_1W_PRESENCE) {
                strobe_led_500(1);
                continue;
            }
            if (send_readout(LPC_I2C_ADDRESS, addr, T) != 0) {
                strobe_led_500(3);
            }
            _delay_ms(1);
        }

        systick_wait_for(10000);
    }

    return 0;
}
