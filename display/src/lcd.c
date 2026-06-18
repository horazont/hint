#include "lcd.h"

#include "lpc111x.h"

#include "gpio/gpio.h"

#include "utils.h"

#define LCD_GPIO            GPIO_GPIO2DATA

// GPIO 2
#define LCD_DATA_MASK       (0xff)
#define LCD_RST_MASK        (1 << 8)
#define LCD_WR_MASK         (1 << 9)
#define LCD_RS_MASK         (1 << 10)
#define LCD_CS_MASK         (1 << 11)
#define LCD_CTRL_MASK       (0xf00)

// GPIO 3
#define LCD_RD_MASK         (1 << 5)

// initalization vm
#define VCMD_COMMAND        0x40
#define VCMD_DATA           0x80
#define VCMD_SLEEP          0xC0

#define LCD_MASKED_GPIO(mask, value) *(pREG32(GPIO_GPIO2_BASE | (mask << 2))) = value

static uint16_t lcd_brightness_goal VAR_RAM = 0xC000;
static uint16_t lcd_brightness_awake_backup VAR_RAM = 0xC000;

/* alphabetical order broken so that inlines appear before their use */

static inline void _configure_pwm()
{
    GPIO_GPIO1DIR |= (1<<9);
    GPIO_GPIO1DATA &= ~(1<<9);
    IOCON_PIO1_9 = (0x1<<0);
}

static inline void _disable_pwm()
{
    TMR_TMR16B0TCR = 0; // pwm timer
    TMR_TMR16B1TCR = 0; // fade timer
    IOCON_PIO1_9 = 0;
    GPIO_GPIO1DATA &= ~(1<<9);
}

static inline void _enable_pwm()
{
    GPIO_GPIO1DATA &= ~(1<<9);
    TMR_TMR16B0TC = 0;
    TMR_TMR16B0PC = 0;
    TMR_TMR16B0TCR = 1; // pwm timer
    IOCON_PIO1_9 = (0x1<<0);
    TMR_TMR16B1TC = 0;
    TMR_TMR16B1PC = 0;
    TMR_TMR16B1TCR = 1; // fade timer
}

void lcd_wrcmd8(uint8_t cmd)
{
    LCD_MASKED_GPIO(LCD_RS_MASK, 0);
    LCD_MASKED_GPIO(LCD_WR_MASK, 0);
    LCD_MASKED_GPIO(LCD_DATA_MASK, cmd);
    NOP();
    LCD_MASKED_GPIO(LCD_WR_MASK, LCD_WR_MASK);
    LCD_MASKED_GPIO(LCD_RS_MASK, LCD_RS_MASK);
}

void lcd_wrdata8(uint8_t data)
{
    LCD_MASKED_GPIO(LCD_WR_MASK, 0);
    LCD_MASKED_GPIO(LCD_DATA_MASK, data);
    NOP();
    LCD_MASKED_GPIO(LCD_WR_MASK, LCD_WR_MASK);
}

void lcd_wrdata16(uint16_t data)
{
    lcd_wrdata8((data >> 8) & 0xff);
    lcd_wrdata8(data & 0xff);
}

void lcd_draw(uint16_t colour)
{
    lcd_wrdata16(colour);
}

void lcd_disable()
{
    LCD_MASKED_GPIO(LCD_CS_MASK, LCD_CS_MASK);
}

void lcd_enable()
{
    LCD_MASKED_GPIO(LCD_CS_MASK, 0);
}

void lcd_drawstart()
{
    lcd_wrcmd8(LCD_CMD_WRITE);
}

void lcd_drawstop()
{

}

void lcd_init()
{
    const uint8_t MEM_BGR = 3;
    const uint8_t MEM_X = 6;
    const uint8_t MEM_Y = 7;

    uint8_t init_sequence[] = {
        VCMD_COMMAND | 1, LCD_CMD_RESET,
        VCMD_SLEEP   |20,
        VCMD_COMMAND | 1, LCD_CMD_DISPLAY_OFF,
        VCMD_SLEEP   |20,
        VCMD_COMMAND | 1, LCD_CMD_POWER_CTRLB,
        VCMD_DATA    | 3, 0x00, 0x83, 0x30, //0x83 0x81 0xAA
        VCMD_COMMAND | 1, LCD_CMD_POWERON_SEQ_CTRL,
        VCMD_DATA    | 4, 0x64, 0x03, 0x12, 0x81, //0x64 0x67
        VCMD_COMMAND | 1, LCD_CMD_DRV_TIMING_CTRLA,
        VCMD_DATA    | 3, 0x85, 0x01, 0x79, //0x79 0x78
        VCMD_COMMAND | 1, LCD_CMD_POWER_CTRLA,
        VCMD_DATA    | 5, 0x39, 0X2C, 0x00, 0x34, 0x02,
        VCMD_COMMAND | 1, LCD_CMD_PUMP_RATIO_CTRL,
        VCMD_DATA    | 1, 0x20,
        VCMD_COMMAND | 1, LCD_CMD_DRV_TIMING_CTRLB,
        VCMD_DATA    | 2, 0x00, 0x00,
        VCMD_COMMAND | 1, LCD_CMD_POWER_CTRL1,
        VCMD_DATA    | 1, 0x26, //0x26 0x25
        VCMD_COMMAND | 1, LCD_CMD_POWER_CTRL2,
        VCMD_DATA    | 1, 0x11,
        VCMD_COMMAND | 1, LCD_CMD_VCOM_CTRL1,
        VCMD_DATA    | 2, 0x35, 0x3E,
        VCMD_COMMAND | 1, LCD_CMD_VCOM_CTRL2,
        VCMD_DATA    | 1, 0xBE, //0xBE 0x94
        VCMD_COMMAND | 1, LCD_CMD_FRAME_CTRL,
        VCMD_DATA    | 2, 0x00, 0x1B, //0x1B 0x70
        VCMD_COMMAND | 1, LCD_CMD_ENABLE_3G,
        VCMD_DATA    | 1, 0x08, //0x08 0x00
        VCMD_COMMAND | 1, LCD_CMD_GAMMA,
        VCMD_DATA    | 1, 0x01, //G2.2
        VCMD_COMMAND | 1, LCD_CMD_POS_GAMMA,
        VCMD_DATA    |15, 0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0x87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00,
        VCMD_COMMAND | 1, LCD_CMD_NEG_GAMMA,
        VCMD_DATA    |15, 0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F,
        VCMD_COMMAND | 1, LCD_CMD_DISPLAY_CTRL,
        VCMD_DATA    | 4, 0x0A, 0x82, 0x27, 0x00,
        VCMD_COMMAND | 1, LCD_CMD_ENTRY_MODE,
        VCMD_DATA    | 1, 0x07,
        VCMD_COMMAND | 1, LCD_CMD_PIXEL_FORMAT,
        VCMD_DATA    | 1, 0x55, //16bit
        VCMD_COMMAND | 1, LCD_CMD_MEMACCESS_CTRL,
        VCMD_DATA    | 1, (1<<MEM_BGR) | (1<<MEM_X) | (1<<MEM_Y),
        VCMD_COMMAND | 1, LCD_CMD_COLUMN,
        VCMD_DATA    | 2, 0x00, 0x00,
        VCMD_DATA    | 2, ((LCD_HEIGHT-1)>>8)&0xFF, (LCD_HEIGHT-1)&0xFF,
        VCMD_COMMAND | 1, LCD_CMD_PAGE,
        VCMD_DATA    | 2, 0x00, 0x00,
        VCMD_DATA    | 2, ((LCD_WIDTH-1)>>8)&0xFF, (LCD_WIDTH-1)&0xFF,
        VCMD_COMMAND | 1, LCD_CMD_SLEEPOUT,
        VCMD_SLEEP   |60,
        VCMD_SLEEP   |60,
        VCMD_COMMAND | 1, LCD_CMD_DISPLAY_ON,
        VCMD_SLEEP   |20,
    };

    DISABLE_IRQ();

    // initialize pins
    IOCON_PIO2_0  &= ~IOCON_PIO2_0_FUNC_MASK;
    IOCON_PIO2_1  &= ~IOCON_PIO2_1_FUNC_MASK;
    IOCON_PIO2_2  &= ~IOCON_PIO2_2_FUNC_MASK;
    IOCON_PIO2_3  &= ~IOCON_PIO2_3_FUNC_MASK;
    IOCON_PIO2_4  &= ~IOCON_PIO2_4_FUNC_MASK;
    IOCON_PIO2_5  &= ~IOCON_PIO2_5_FUNC_MASK;
    IOCON_PIO2_6  &= ~IOCON_PIO2_6_FUNC_MASK;
    IOCON_PIO2_7  &= ~IOCON_PIO2_7_FUNC_MASK;
    IOCON_PIO2_8  &= ~IOCON_PIO2_8_FUNC_MASK;
    IOCON_PIO2_9  &= ~IOCON_PIO2_9_FUNC_MASK;
    IOCON_PIO2_10 &= ~IOCON_PIO2_10_FUNC_MASK;
    IOCON_PIO2_11 &= ~IOCON_PIO2_11_FUNC_MASK;

    IOCON_PIO3_5  &= ~IOCON_PIO3_5_FUNC_MASK;

    // set all 12 pins to output mode
    GPIO_GPIO2DIR |= 0xFFF;
    // set all pins high
    LCD_GPIO |= 0xFFF;

    // set pin 5 of gpio 3 to output mode
    GPIO_GPIO3DIR |= LCD_RD_MASK;
    GPIO_GPIO3DATA |= LCD_RD_MASK;

    ENABLE_IRQ();

    // trigger hard-reset
    LCD_MASKED_GPIO(LCD_RST_MASK, 0);
    delay_ms(20);
    LCD_MASKED_GPIO(LCD_RST_MASK, LCD_RST_MASK);
    delay_ms(120);

    lcd_enable();
    delay_ms(1);

    for (unsigned int i = 0; i < sizeof(init_sequence);) {
        uint8_t instruction = init_sequence[i++];
        switch (instruction & 0xC0) {
        case VCMD_COMMAND:
        {
            //~ printf("cmd 0x%02x", instruction & 0x3f);
            for (int j = (instruction & 0x3f); j > 0; j--) {
                uint8_t data = init_sequence[i++];
                //~ printf(" 0x%02x", data);
                lcd_wrcmd8(data);
            }
            //~ printf("\n");
            break;
        }
        case VCMD_DATA:
        {
            //~ printf("data 0x%02x\n", instruction & 0x3f);
            for (int j = (instruction & 0x3f); j > 0; j--) {
                uint8_t data = init_sequence[i++];
                //~ printf(" 0x%02x", data);
                lcd_wrdata8(data);
            }
            //~ printf("\n");
            break;
        }
        case VCMD_SLEEP:
        {
            //~ printf("sleep %d\n", instruction & 0x3f);
            delay_ms(instruction & 0x3f);
            break;
        }
        default:
            // cannot happen :)
            continue;
        };
    }

    lcd_drawstart();
    for (int i = (LCD_WIDTH*LCD_HEIGHT/8); i > 0; i--) {
        lcd_draw(0);
        lcd_draw(0);
        lcd_draw(0);
        lcd_draw(0);
        lcd_draw(0);
        lcd_draw(0);
        lcd_draw(0);
        lcd_draw(0);
    }
    lcd_drawstop();

    lcd_disable();
}

void lcd_init_backlight(uint16_t initial_brightness)
{
    _configure_pwm();

    // display backlight fading timer
    TMR_TMR16B0TC = 0;
    TMR_TMR16B0PR = 24000-1; // prescale to two ticks per ms
    TMR_TMR16B0PC = 0;
    TMR_TMR16B0MCR = TMR_TMR16B0MCR_MR0_INT_ENABLED
                   | TMR_TMR16B0MCR_MR0_RESET_ENABLED;
    TMR_TMR16B0MR0 = 20; // fade update interval

    // display backlight pwm
    TMR_TMR16B1TC   = 0;
    TMR_TMR16B1PR   = 0; //no prescale
    TMR_TMR16B1PC   = 0;
    TMR_TMR16B1CTCR = 0;
    TMR_TMR16B1MR0  = 0xFFFF - initial_brightness;
    TMR_TMR16B1MR3  = 0xFFFF;
    TMR_TMR16B1MCR  = TMR_TMR16B1MCR_MR3_RESET_ENABLED;
    TMR_TMR16B1PWMC = TMR_TMR16B1PWMC_PWM0_ENABLED
                    | TMR_TMR16B1PWMC_PWM3_ENABLED; //PWM chn 0 on

    _enable_pwm();
}

void lcd_lullaby()
{
    lcd_brightness_awake_backup = lcd_brightness_goal;

    // slower going to sleep with visual effects :-O
    // fading from full power to black requires 140 steps, we speed up fading
    // for this purpose, each step is MR0 times 0.5 ms, so 350 ms if MR0 is 5
    lcd_setbrightness(0);

    NVIC_DisableIRQ(TIMER_16_0_IRQn);
    TMR_TMR16B0MR0 = 5;
    NVIC_EnableIRQ(TIMER_16_0_IRQn);

    // half way through, we switch the display into idle mode
    delay_ms(350);
    // lcd should be close to black now, we turn it off
    lcd_wrcmd8(LCD_CMD_SLEEPIN);

    // disable PWM
    DISABLE_IRQ();
    _disable_pwm();
    TMR_TMR16B0MR0 = 20;
    ENABLE_IRQ();

    // wait until SLEEPIN completes
    delay_ms(120);
}

void lcd_put_to_sleep()
{
    lcd_wrcmd8(LCD_CMD_SLEEPIN);
    DISABLE_IRQ();
    _disable_pwm();
    ENABLE_IRQ();
    delay_ms(120);
}

void lcd_setarea(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_wrcmd8(LCD_CMD_COLUMN);
    lcd_wrdata16(y0);
    lcd_wrdata16(y1);

    lcd_wrcmd8(LCD_CMD_PAGE);
    lcd_wrdata16(x0);
    lcd_wrdata16(x1);
}

void lcd_setbrightness(uint16_t new_brightness)
{
    lcd_brightness_goal = 0xffff - new_brightness;
}

void lcd_setbrightness_nofade(uint16_t new_brightness)
{
    lcd_brightness_goal = 0xffff - new_brightness;
    TMR_TMR16B1MR0 = 0xffff - new_brightness;
}

inline void lcd_setpixel(const uint16_t x0, const uint16_t y0,
                         const colour_t colour)
{
    lcd_setarea(x0, y0, x0, y0);
    lcd_drawstart();
    lcd_draw(colour);
    lcd_drawstop();
}

void lcd_wakeup()
{
    lcd_wrcmd8(LCD_CMD_SLEEPOUT);
    delay_ms(120);
    DISABLE_IRQ();
    // set pwm to black
    TMR_TMR16B1MR0 = 0xffff;
    _enable_pwm();
    ENABLE_IRQ();
    lcd_setbrightness(lcd_brightness_awake_backup);
}

void TIMER16_0_IRQHandler()
{
    uint32_t intermediate = TMR_TMR16B1MR0;
    // moving average for smoooooooooooth fading :)
    intermediate = intermediate * 15 + lcd_brightness_goal;
    TMR_TMR16B1MR0 = intermediate / 16;
    TMR_TMR16B0IR = TMR_TMR16B0IR_MR0;
}
