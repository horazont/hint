#include "projectconfig.h"

#include "uart/uart.h"
#include "cpu/cpu.h"
#include "systick/systick.h"

#include "lcd.h"
#include "utils.h"
#include "font.h"
#include "font_data.h"
#include "draw.h"
#include "graphs.h"

#include "lpc111x.h"

#include <string.h>

uint8_t uartRecvByte()
{
    while (!uartRxBufferDataPending()) {

    }
    return uartRxBufferRead();
}

//static uint8_t data[] = {'\x3c', '\x46', '\x02', '\x3e', '\x42', '\x46', '\x3a'};
static const uint8_t text[] = "This is a long test text which should be ellipsized.";
static const uint8_t text2[] = "…";

static const struct data_point_t data[] = {
        {.x = 0, .y = 0},
        {.x = 10, .y = 10},
        {.x = 20, .y = 5},
        {.x = 30, .y = 0},
        {.x = 40, .y = 5}
    };

int main(void)
{
    struct graph_axes_t axes;
    axes.x0 = 10;
    axes.y0 = 10;
    axes.width = 240;
    axes.height = 40;

    // Configure cpu and mandatory peripherals
    cpuInit();
    cpuPllSetup(CPU_MULTIPLIER_3);
    systickInit((CFG_CPU_CCLK / 1000) * CFG_SYSTICK_DELAY_IN_MS);


    DISABLE_IRQ();

    SCB_SYSAHBCLKCTRL |= SCB_SYSAHBCLKCTRL_GPIO
                      |  SCB_SYSAHBCLKCTRL_IOCON
                      |  SCB_SYSAHBCLKCTRL_SYS
                      |  SCB_SYSAHBCLKCTRL_CT16B1;

    GPIO_GPIO1DIR |= (1<<9);
    GPIO_GPIO1DATA &= ~(1<<9);

    // display backlight pwm
    IOCON_PIO1_9 = (0x1<<0); //PIO1_9/CT16B1MAT0 -> PWM
    TMR_TMR16B1TC   = 0;
    TMR_TMR16B1PR   = 0; //no prescale
    TMR_TMR16B1PC   = 0;
    TMR_TMR16B1CTCR = 0;
    TMR_TMR16B1MR0  = 0x00;
    TMR_TMR16B1MR3  = 0xFF;
    TMR_TMR16B1MCR  = TMR_TMR16B1MCR_MR3_RESET_ENABLED;
    TMR_TMR16B1PWMC = TMR_TMR16B1PWMC_PWM0_ENABLED | TMR_TMR16B1PWMC_PWM3_ENABLED; //PWM chn 0 on
    TMR_TMR16B1TCR  = (1<<0); //enable timer

    ENABLE_IRQ();

    lcd_init();
    lcd_enable();

    fill_rectangle(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1, 0x0000);

    graph_background(&axes, 0x2222);
    graph_x_axis(&axes, 0x5555, 20);
    graph_y_axis(&axes, 0x5555, 0);
    graph_line(&axes, data, 5, 0xf000, LINE_STEP);

    font_draw_text_ellipsis(
        &cantarell_12px,
        20, 80, 0xffff,
        text,
        100);

    lcd_disable();

    return 0;
}
