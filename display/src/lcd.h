#ifndef _LCD_H
#define _LCD_H

#include <stdint.h>

#include "common/types.h"

struct point_t {
    coord_int_t x, y;
};

#define LCD_WIDTH (320)
#define LCD_HEIGHT (240)

enum lcd_command_t {
    LCD_CMD_NOP =                  0x00,
    LCD_CMD_RESET =                0x01,
    LCD_CMD_SLEEPIN =              0x10,
    LCD_CMD_SLEEPOUT =             0x11,
    LCD_CMD_PARTIAL_MODE =         0x12,
    LCD_CMD_NORMAL_MODE =          0x13,
    LCD_CMD_INV_OFF =              0x20,
    LCD_CMD_INV_ON =               0x21,
    LCD_CMD_GAMMA =                0x26,
    LCD_CMD_DISPLAY_OFF =          0x28,
    LCD_CMD_DISPLAY_ON =           0x29,
    LCD_CMD_COLUMN =               0x2A,
    LCD_CMD_PAGE =                 0x2B,
    LCD_CMD_WRITE =                0x2C,
    LCD_CMD_READ =                 0x2E,
    LCD_CMD_PARTIAL_AREA =         0x30,
    LCD_CMD_TEARING_OFF =          0x34,
    LCD_CMD_TEARING_ON =           0x35,
    LCD_CMD_MEMACCESS_CTRL =       0x36,
    LCD_CMD_IDLE_OFF =             0x38,
    LCD_CMD_IDLE_ON =              0x39,
    LCD_CMD_PIXEL_FORMAT =         0x3A,
    LCD_CMD_WRITE_CNT =            0x3C,
    LCD_CMD_READ_CNT =             0x3E,
    LCD_CMD_BRIGHTNESS =           0x51,
    LCD_CMD_BRIGHTNESS_CTRL =      0x53,
    LCD_CMD_RGB_CTRL =             0xB0,
    LCD_CMD_FRAME_CTRL =           0xB1, //normal mode
    LCD_CMD_FRAME_CTRL_IDLE =      0xB2, //idle mode
    LCD_CMD_FRAME_CTRL_PART =      0xB3, //partial mode
    LCD_CMD_INV_CTRL =             0xB4,
    LCD_CMD_DISPLAY_CTRL =         0xB6,
    LCD_CMD_ENTRY_MODE =           0xB7,
    LCD_CMD_POWER_CTRL1 =          0xC0,
    LCD_CMD_POWER_CTRL2 =          0xC1,
    LCD_CMD_VCOM_CTRL1 =           0xC5,
    LCD_CMD_VCOM_CTRL2 =           0xC7,
    LCD_CMD_POWER_CTRLA =          0xCB,
    LCD_CMD_POWER_CTRLB =          0xCF,
    LCD_CMD_POS_GAMMA =            0xE0,
    LCD_CMD_NEG_GAMMA =            0xE1,
    LCD_CMD_DRV_TIMING_CTRLA =     0xE8,
    LCD_CMD_DRV_TIMING_CTRLB =     0xEA,
    LCD_CMD_POWERON_SEQ_CTRL =     0xED,
    LCD_CMD_ENABLE_3G =            0xF2,
    LCD_CMD_INTERF_CTRL =          0xF6,
    LCD_CMD_PUMP_RATIO_CTRL =      0xF7
};

void lcd_disable();
void lcd_draw(const colour_t colour);
void lcd_drawstart();
void lcd_drawstop();
void lcd_enable();
void lcd_init();
/* This must be called with interrupts disabled! */
void lcd_init_backlight(uint16_t initial_brightness);
void lcd_lullaby();
void lcd_put_to_sleep();
void lcd_reset();
void lcd_setarea(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_setbrightness(uint16_t new_brightness);
void lcd_setbrightness_nofade(uint16_t new_brightness);
void lcd_setpixel(const uint16_t x0, const uint16_t y0, const colour_t colour);
void lcd_wakeup();
void lcd_wrcmd8(uint8_t cmd);
void lcd_wrdata8(uint8_t data);
void lcd_wrdata16(uint16_t data);


#endif
