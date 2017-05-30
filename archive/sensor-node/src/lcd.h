#ifndef LCD_H
#define LCD_H

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <avr/io.h>
#include <util/delay.h>

#define LCD_DATA_SHIFT (2)
#define LCD_DATA_MASK (0x0F << LCD_DATA_SHIFT)
#define LCD_ENABLE_SHIFT (3)
#define LCD_RS_SHIFT (6)
#define LCD_BACKLIGHT_SHIFT (2)
#define LCD_MAX_BACKLIGHT (11)

typedef uint8_t lcd_addr_t;

extern lcd_addr_t lcd_curr_row;
extern lcd_addr_t lcd_curr_col;
extern const __flash uint8_t lcd_duty_cycles[LCD_MAX_BACKLIGHT+1];

static inline void lcd_set4(uint8_t nybble)
{
    PORTD = (PORTD & ~LCD_DATA_MASK) | ((nybble & 0x0F) << LCD_DATA_SHIFT);
}

static inline void lcd_seten(bool enable)
{
    if (enable) {
        PORTB |= (1<<LCD_ENABLE_SHIFT);
    } else {
        PORTB &= ~(1<<LCD_ENABLE_SHIFT);
    }
}

static inline void lcd_setrs(bool rs)
{
    if (rs) {
        PORTD |= (1<<LCD_RS_SHIFT);
    } else {
        PORTD &= ~(1<<LCD_RS_SHIFT);
    }
}

static inline void lcd_pulseen()
{
    lcd_seten(true);
    _delay_us(1);
    lcd_seten(false);
    _delay_us(100);
}

static inline void lcd_write4(uint8_t value)
{
    lcd_set4(value);
    lcd_pulseen();
}

static inline void lcd_write8as4(uint8_t value)
{
    lcd_write4(value >> 4);
    lcd_write4(value);
}

/**
 * Set up the MCU peripherials and I/O port configuration for the LCD
 * subsystem.
 *
 * This does not send any commands to the LCD itself. For LCD
 * initialization, see reset().
 */
static inline void lcd_init()
{
    lcd_curr_row = 0;
    lcd_curr_col = 0;

    // set PWM mode
    TCCR0A = (1<<WGM00) | (1<<WGM01) | (1<<COM0A1);
    OCR0A = 0x7F;
    // timer clock: F_CPU / 8
    TCCR0B = (1<<CS01);
}

void lcd_clear();
void lcd_config(bool enable, bool blink_cursor);

void lcd_line_feed();

static inline void lcd_set_backlight(uint8_t level)
{
    OCR0A = lcd_duty_cycles[level];
}

void lcd_set_cursor(lcd_addr_t row, lcd_addr_t col);

void lcd_write_data(uint8_t value);
void lcd_write_instr(uint8_t value);
void lcd_write_textbuf(const char *buf, size_t len);
void lcd_write_textch(const char ch);

void lcd_write_textbuf_from_flash(const __flash char *ch, size_t len);
#define lcd_write_text_from_flash(text) lcd_write_textbuf_from_flash(\
        text, sizeof(text)-1)

/* static inline void lcd_write_text(const char *buf) */
/* { */
/*     lcd_write_textbuf(buf, strlen(buf)); */
/* } */

/**
 * Reset and initialize the LCD. This requires that the power is
 * well-established at the LCD. It is recommended to wait about 50 ms for
 * the power to raise after RESET.
 */
static inline void lcd_reset()
{
    lcd_write4(0x3);
    _delay_ms(5);
    lcd_write4(0x3);
    _delay_ms(5);
    lcd_write4(0x3);
    _delay_us(150);
    lcd_write4(0x2);

    // 0x28 => 0011 (cmd) & 1 (two lines) & 0 (default font) & 00
    lcd_write_instr(0x28);
    lcd_write_instr(0x08);
    lcd_write_instr(0x01);
    lcd_write_instr(0x06);

    _delay_ms(10);

    lcd_write_instr(0x0C);
    lcd_clear();

    lcd_curr_row = 0;
    lcd_curr_col = 0;
}

extern const __flash lcd_addr_t lcd_row_base_table[4];

static inline void lcd_update_cursor()
{
    lcd_addr_t cursor_addr = lcd_curr_col + lcd_row_base_table[lcd_curr_row];
    lcd_write_instr(0x80 | cursor_addr);
}

#endif
