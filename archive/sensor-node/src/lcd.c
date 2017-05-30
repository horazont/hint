#include "lcd.h"

#include <avr/io.h>
#include <util/delay.h>

uint_least8_t lcd_curr_col;
uint_least8_t lcd_curr_row;

const __flash lcd_addr_t lcd_row_base_table[4] = {0x00, 0x40, 0x14, 0x54};

const __flash uint8_t lcd_duty_cycles[LCD_MAX_BACKLIGHT+1] = {
    0x00, 0x01, 0x02, 0x03, 0x05, 0x09, 0x0F, 0x18, 0x27, 0x3F, 0x65, 0xA0
};

/* public functions */

void lcd_clear()
{
    lcd_write_instr(0x01);
    _delay_ms(2);
    lcd_curr_row = 0;
    lcd_curr_col = 0;
}

void lcd_config(bool enable, bool blink_cursor)
{
    uint8_t command = 0x08;
    if (enable) {
        command |= 0x4;
    }
    if (blink_cursor) {
        command |= 0x1;
    }
    lcd_write_instr(command);
}

void lcd_line_feed()
{
    lcd_curr_row += 1;
    lcd_curr_col = 0;
    if (lcd_curr_row == 4) {
        lcd_curr_row = 0;
    }
    lcd_update_cursor();
}

void lcd_set_cursor(lcd_addr_t row, lcd_addr_t col)
{
    lcd_curr_row = row;
    if (row >= 4) {
        lcd_curr_row = 3;
    }
    lcd_curr_col = col;
    if (col >= 20) {
        lcd_curr_row = 19;
    }
    lcd_update_cursor();
}

inline void lcd_write_data(uint8_t value)
{
    lcd_setrs(true);
    lcd_write8as4(value);
    lcd_curr_col += 1;
    if (lcd_curr_col == 20) {
        lcd_curr_row += 1;
        lcd_curr_col = 0;
        if (lcd_curr_row == 4) {
            lcd_curr_row = 0;
        }

        lcd_update_cursor();
    }
}

void lcd_write_instr(uint8_t value)
{
    lcd_setrs(false);
    lcd_write8as4(value);
}

void lcd_write_textch(const char ch)
{
    if (ch == '\n') {
        lcd_line_feed();
    } else {
        lcd_write_data(ch);
    }
}

void lcd_write_textbuf(const char *buf, size_t len)
{
    const char *end = &buf[len];
    while (buf != end) {
        lcd_write_textch(*buf++);
    }
}

void lcd_write_textbuf_from_flash(const __flash char *buf, size_t len)
{
    const __flash char *end = &buf[len];
    while (buf != end) {
        lcd_write_textch(*buf++);
    }
}
