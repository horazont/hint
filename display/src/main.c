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
#include "tables.h"
#include "touch.h"
#include "comm.h"
#include "time.h"

#include "lpc111x.h"

#include <string.h>

#define IOCON_PIO                      (0x00<<0) //pio
#define IOCON_R_PIO                    (0x01<<0) //pio (reserved pins)
#define IOCON_ADC                      (0x01<<0) //adc
#define IOCON_R_ADC                    (0x02<<0) //adc (reserved pins)
#define IOCON_NOPULL                   (0x00<<3) //no pull-down/pull-up
#define IOCON_PULLDOWN                 (0x01<<3) //pull-down
#define IOCON_PULLUP                   (0x02<<3) //pull-up
#define IOCON_ANALOG                   (0x00<<7) //analog (adc pins)
#define IOCON_DIGITAL                  (0x01<<7) //digital (adc pins)


uint8_t uartRecvByte()
{
    while (!uartRxBufferDataPending()) {

    }
    return uartRxBufferRead();
}

//static uint8_t data[] = {'\x3c', '\x46', '\x02', '\x3e', '\x42', '\x46', '\x3a'};
static const uint8_t text[] = "Ellipsized long test text.";
static const uint8_t text2[] = "…";

//~ static const char *texts[] = {
    //~ "A", "Löbtau", "-1",
    //~ "62", "Dölzschen", "1",
    //~ "63", "Löbtau", "3",
    //~ "85", "Löbtau Süd", "10",
//~ };

static const uint8_t buffer_src[] = "x=0x0000; y=0x0000; z=0x0000";

static const struct data_point_t data[] = {
    {.x = 0, .y = 0},
    {.x = 10, .y = 10},
    {.x = 20, .y = 5},
    {.x = 30, .y = 0},
    {.x = 40, .y = 5}
};

static const struct table_column_t columns[] = {
    {.width = 20},
    {.width = 120},
    {.width = 20}
};

static const uint8_t hexmap[16] VAR_FLASH = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

void coord_to_hex(coord_int_t c, uint8_t *dest)
{
    uint16_t v = (uint16_t)(c);
    uint_least16_t shift = 16;

    do {
        shift -= 4;
        *dest = hexmap[(v >> shift) & 0xF];
        dest++;
    } while (shift);
}

void uint32_to_hex(const uint32_t c, uint8_t *dest)
{
    uint_least32_t shift = 32;

    do {
        shift -= 4;
        *dest = hexmap[(c >> shift) & 0xF];
        dest++;
    } while (shift);
}


//~ void memcpy(void *dest, const void *src, int count)
//~ {
    //~ for (int i = 0; i < count; i++) {
        //~ *((uint8_t*)dest) = *((const uint8_t*)src);
    //~ }
//~ }

static int irq_called VAR_RAM = 0;
static volatile bool msg_pending = false;

void ADC_IRQHandler(void)
{
    // only used for touch right now
    irq_called = 1;
    touch_intr_sm();
}

void PIOINT0_IRQHandler(void)
{
    struct msg_buffer_t *msg = comm_get_rx_message();

    if (msg->msg.header.payload_length == 0) {
        comm_release_rx_message();
        return;
    }

    msg_pending = true;
}

void HardFault_Handler(void)
{
    lcd_enable();
    fill_rectangle(0, 0, LCD_WIDTH-1, 16, 0xf000);
    lcd_disable();
    while (1);
}

void show_coords(uint8_t *buffer, coord_int_t x, coord_int_t y, coord_int_t z)
{
    coord_to_hex(x, &buffer[4]);
    coord_to_hex(y, &buffer[14]);
    coord_to_hex(z, &buffer[24]);
    fill_rectangle(
        120, 0, 320, 20, 0x0000);
    font_draw_text(
        &cantarell_12px,
        120, 12,
        0xffff,
        buffer);

}

enum event_t {
    EV_NONE,
    EV_TOUCH,
    EV_COMM
};

static struct ticks_t last_touch_sample VAR_RAM = {
    .rollovers = 0,
    .ticks = 0
};


#define TOUCH_SAMPLE_INTERVAL (50)

static inline enum event_t wait_for_event()
{
    struct ticks_t t1 = ticks_get();
    while (1) {
        do {
            if (msg_pending) {
                msg_pending = false;
                return EV_COMM;
            }
            t1 = ticks_get();
        } while (ticks_delta(&last_touch_sample, &t1) < TOUCH_SAMPLE_INTERVAL);
        touch_sample();
        last_touch_sample = ticks_get();
        if (touch_get_raw_z() != 0) {
            return EV_TOUCH;
        }
    }
}

int main(void)
{

    //~ struct graph_axes_t axes;
    //~ struct table_t tbl;
    uint8_t buffer[30];
    memcpy(buffer, buffer_src, 29);
    buffer[29] = '\0';

    //~ axes.x0 = 10;
    //~ axes.y0 = 10;
    //~ axes.width = 240;
    //~ axes.height = 40;

    // Configure cpu and mandatory peripherals
    cpuInit();
    cpuPllSetup(CPU_MULTIPLIER_3);
    systickInit((CFG_CPU_CCLK / 1000) * CFG_SYSTICK_DELAY_IN_MS);

    comm_init(115200);

    DISABLE_IRQ();

    SCB_PDRUNCFG &= ~SCB_PDRUNCFG_ADC;
    //~ NVIC_EnableIRQ(ADC_IRQn);

    SCB_SYSAHBCLKCTRL |= SCB_SYSAHBCLKCTRL_GPIO
                      |  SCB_SYSAHBCLKCTRL_IOCON
                      |  SCB_SYSAHBCLKCTRL_SYS
                      |  SCB_SYSAHBCLKCTRL_CT16B1
                      |  SCB_SYSAHBCLKCTRL_ADC;

    GPIO_GPIO1DIR |= (1<<9);
    GPIO_GPIO1DATA &= ~(1<<9);

    // display backlight pwm
    IOCON_PIO1_9 = (0x1<<0); //PIO1_9/CT16B1MAT0 -> PWM
    TMR_TMR16B1TC   = 0;
    TMR_TMR16B1PR   = 0; //no prescale
    TMR_TMR16B1PC   = 0;
    TMR_TMR16B1CTCR = 0;
    TMR_TMR16B1MR0  = 0x8000;
    TMR_TMR16B1MR3  = 0xFFFF;
    TMR_TMR16B1MCR  = TMR_TMR16B1MCR_MR3_RESET_ENABLED;
    TMR_TMR16B1PWMC = TMR_TMR16B1PWMC_PWM0_ENABLED | TMR_TMR16B1PWMC_PWM3_ENABLED; //PWM chn 0 on
    TMR_TMR16B1TCR  = (1<<0); //enable timer

    ADC_AD0CR = (((CFG_CPU_CCLK/4000000UL)-1)<< 8) | //4MHz
                                          (0<<16) | //burst off
                                        (0x0<<17) | //10bit
                                        (0x0<<24);  //stop
    ENABLE_IRQ();


    lcd_init();
    touch_init();
    lcd_enable();

    fill_rectangle(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1, 0x0000);

    NVIC_EnableIRQ(EINT0_IRQn);
    // use a lower priority for EINT0 so that one can TX and RX inside
    // that interrupt
    NVIC_SetPriority(EINT0_IRQn, 1);

    //~ graph_background(&axes, 0x2222);
    //~ graph_x_axis(&axes, 0x5555, 20);
    //~ graph_y_axis(&axes, 0x5555, 0);
    //~ graph_line(&axes, data, 5, 0xf000, LINE_STEP);
//~
    //~ font_draw_text_ellipsis(
        //~ &cantarell_12px,
        //~ 20, 80, 0xffff,
        //~ text,
        //~ 100);
//~
    //~ table_init(&tbl, columns, 3, 16);
    //~ table_start(&tbl, 20, 100);
    //~ table_row(&tbl, &cantarell_12px, (const uint8_t **)&texts[0], 0xffff);
    //~ table_row(&tbl, &cantarell_12px, (const uint8_t **)&texts[3], 0xffff);
    //~ table_row(&tbl, &cantarell_12px, (const uint8_t **)&texts[6], 0xffff);
    //~ table_row(&tbl, &cantarell_12px, (const uint8_t **)&texts[9], 0xffff);


    //touch_test();

    //~ DISABLE_IRQ();
    //~ IOCON_JTAG_TDI_PIO0_11 = IOCON_JTAG_TDI_PIO0_11_ADMODE_DIGITAL | IOCON_JTAG_TDI_PIO0_11_FUNC_GPIO;
    //~ IOCON_JTAG_TDO_PIO1_1 = IOCON_JTAG_TDO_PIO1_1_ADMODE_ANALOG | IOCON_JTAG_TDO_PIO1_1_FUNC_AD2;
    //~ IOCON_JTAG_TMS_PIO1_0 = IOCON_JTAG_TMS_PIO1_0_ADMODE_ANALOG | IOCON_JTAG_TMS_PIO1_0_FUNC_AD1;
    //~ IOCON_JTAG_nTRST_PIO1_2 = IOCON_JTAG_nTRST_PIO1_2_ADMODE_DIGITAL | IOCON_JTAG_nTRST_PIO1_2_FUNC_GPIO;
//~ //~
    //~ GPIO_GPIO0DIR   |= (1<<11);
    //~ GPIO_GPIO0DATA  &= ~(1<<11);
    //~ GPIO_GPIO1DIR   &= ~(1<<1);
    //~ GPIO_GPIO1DIR   &= ~(1<<0);
    //~ GPIO_GPIO1DIR   |= (1<<2);
    //~ GPIO_GPIO1DATA  |= (1<<2);
    //~ ENABLE_IRQ();

    _Static_assert(sizeof(unsigned int) == 4, "foo");
    {
        coord_int_t x, y, z;
        struct point11_4_t lcd1, touch1, lcd2, touch2;
        fill_rectangle(0, 0, 5, 5, 0xffff);
        lcd1.x = fp11_4_from_int16_t(2) + FP11_4_ZERO_POINT_FIVE;
        lcd1.y = fp11_4_from_int16_t(2) + FP11_4_ZERO_POINT_FIVE;
        touch_wait_for_raw(&x, &y, &z);
        touch_wait_for_clear();
        touch1.x = fp11_4_from_int16_t(x);
        touch1.y = fp11_4_from_int16_t(y);
        fill_rectangle(0, 0, 5, 5, 0x0000);

        show_coords(buffer, x, y, z);

        fill_rectangle(LCD_WIDTH-6, LCD_HEIGHT-6, LCD_WIDTH-1, LCD_HEIGHT-1, 0xffff);
        lcd2.x = fp11_4_from_int16_t(LCD_WIDTH-3) + FP11_4_ZERO_POINT_FIVE;
        lcd2.y = fp11_4_from_int16_t(LCD_HEIGHT-3) + FP11_4_ZERO_POINT_FIVE;
        touch_wait_for_raw(&x, &y, &z);
        touch_wait_for_clear();
        touch2.x = fp11_4_from_int16_t(x);
        touch2.y = fp11_4_from_int16_t(y);
        fill_rectangle(LCD_WIDTH-6, LCD_HEIGHT-6, LCD_WIDTH-1, LCD_HEIGHT-1, 0x0000);

        show_coords(buffer, x, y, z);

        touch_calculate_calibration(&lcd1, &lcd2, &touch1, &touch2, 0);

        touch_wait_for_clear();
    }

    lcd_disable();

    while (1)
    {
        switch (wait_for_event())
        {
        case EV_COMM:
        {
            struct msg_buffer_t *msg = comm_get_rx_message();
            lcd_enable();
            font_draw_text(
                &cantarell_12px,
                0, 24,
                0xffff,
                &msg->msg.data[0]);
            lcd_disable();

            msg->msg.header.recipient = MSG_ADDRESS_HOST;
            msg->msg.header.sender = MSG_ADDRESS_LPC1114;
            comm_tx_message(&msg->msg);

            comm_release_rx_message();
            break;
        }
        case EV_TOUCH:
        {
            coord_int_t x, y;
            x = touch_get_x();
            y = touch_get_y();
            lcd_enable();
            fill_rectangle(x-3, y-3, x+3, y+3, 0x001f);
            lcd_disable();
            break;
        }
        case EV_NONE:
        default:
        {
            // nothing to do
        }
        }
    }

    return 0;
}
