#include "projectconfig.h"

#include "uart/uart.h"
#include "cpu/cpu.h"
#include "systick/systick.h"

#include "lcd.h"
#include "utils.h"
// #include "font.h"
#include "draw.h"

#include "lpc111x.h"

#include <string.h>

uint8_t uartRecvByte()
{
    while (!uartRxBufferDataPending()) {

    }
    return uartRxBufferRead();
}

int main(void)
{
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
    TMR_TMR16B1MR0  = 0xD0;
    TMR_TMR16B1MR3  = 0xFF;
    TMR_TMR16B1MCR  = TMR_TMR16B1MCR_MR3_RESET_ENABLED;
    TMR_TMR16B1PWMC = TMR_TMR16B1PWMC_PWM0_ENABLED | TMR_TMR16B1PWMC_PWM3_ENABLED; //PWM chn 0 on
    TMR_TMR16B1TCR  = (1<<0); //enable timer

    ENABLE_IRQ();

    lcd_init();
    lcd_enable();

    fill_rectangle(10, 10, 20, 30, 0x3333);

    draw_line(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1, 0x3333);
    draw_line(LCD_WIDTH-1, 0, 0, LCD_HEIGHT-1, 0x3333);

    draw_rectangle(15, 15, 80, 80, 0xffff);


    lcd_disable();

    return 0;
}
