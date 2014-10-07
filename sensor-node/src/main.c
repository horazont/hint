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
#include "lcd.h"
// #include "bcd.h"
// #include "systick.h"

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

static inline void lcd_writebool(bool value)
{
    if (value) {
        lcd_write_textch('1');
    } else {
        lcd_write_textch('0');
    }
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

int main()
{
    DDRD = (1<<DDD2) | (1<<DDD3) | (1<<DDD4) | (1<<DDD5) | (1<<DDD6);
    DDRB = (1<<DDB3) | (1<<DDB2) | (1<<DDB4);

    _delay_ms(50);

    /* systick_init(); */
    /* onewire_init(); */
    lcd_init();
    _delay_ms(50);
    lcd_reset();
    lcd_set_backlight(4);

    USI_TWI_Master_Initialise();

    uint8_t ctr = 0x01;
    char hexed[2];
    onewire_addr_t addr;
    /* uint8_t blob[9]; */
    clear_addr(addr);
    while (1) {
        lcd_clear();
        lcd_set_cursor(0, 17);
        uint8_to_hex(hexed, ctr);
        lcd_write_textbuf(hexed, 2);

        uint8_t state = send_readout(LPC_I2C_ADDRESS, addr, 0x1234);
        lcd_set_cursor(0, 0);
        uint8_to_hex(hexed, state);
        lcd_write_textbuf(hexed, 2);

        /* lcd_set_cursor(1, 0); */
        /* for (uint8_t i = 0; i < UART_1W_ADDR_LEN; i++) { */
        /*     uint8_to_hex(hexed, addr[i]); */
        /*     lcd_write_textbuf(hexed, 2); */
        /* } */

        /* lcd_set_cursor(0, 0); */
        /* uint8_t status = onewire_findnext(addr); */
        /* uint8_to_hex(hexed, status); */
        /* lcd_write_textbuf(hexed, 2); */

        /* lcd_set_cursor(2, 0); */
        /* for (uint8_t i = 0; i < UART_1W_ADDR_LEN; i++) { */
        /*     uint8_to_hex(hexed, addr[i]); */
        /*     lcd_write_textbuf(hexed, 2); */
        /* } */

        /* if (status != UART_1W_PRESENCE) { */
        /*     // reset address */
        /*     clear_addr(addr); */
        /*     onewire_ds18b20_broadcast_conversion(); */
        /* } else { */
        /*     onewire_ds18b20_read_scratchpad(addr, blob); */
        /*     lcd_set_cursor(3, 0); */
        /*     for (uint8_t i = 0; i < 9; i++) { */
        /*         uint8_to_hex(hexed, blob[i]); */
        /*         lcd_write_textbuf(hexed, 2); */
        /*     } */

        /* } */

        /* lcd_set_cursor(1, 0); */
        /* uint8_to_hex(hexed, onewire_control_probe(0xF0)); */
        /* lcd_write_textbuf(hexed, 2); */
        /* onewire_write_byte(0x33); */
        /* uint8_to_hex(hexed, onewire_probe(0xFF)); */
        /* lcd_write_textbuf(hexed, 2); */
        /* uint8_to_hex(hexed, onewire_probe(0xFF)); */
        /* lcd_write_textbuf(hexed, 2); */

        /* lcd_set_cursor(1, 0); */
        /* onewire_reset(); */
        /* onewire_write_byte(0xF0); */
        /* lcd_writebool(onewire_read()); */
        /* lcd_writebool(onewire_read()); */


        ctr += 1;
        if (ctr > 0x7F) {
            ctr = 0x01;
        }
        _delay_ms(500);
    }

    return 0;
}
