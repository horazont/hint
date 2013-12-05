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
#include "common/comm_lpc1114.h"
#include "time.h"
#include "buffer.h"

#include "lpc111x.h"

#include <string.h>

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

    if (HDR_GET_PAYLOAD_LENGTH(msg->msg.header) == 0) {
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
        &dejavu_sans_12px,
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

static coord_int_t prevz VAR_RAM = 0;

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
            prevz = 1;
            return EV_TOUCH;
        } else if (prevz > 0) {
            prevz = 0;
            return EV_TOUCH;
        }
    }
}

static inline coord_int_t abs(const coord_int_t v)
{
    return (v > 0 ? v : -v);
}

enum command_state_t {
    STATE_IDLE,
    STATE_DRAWING_IMAGE,
    STATE_TABLE
};

static struct lpc_cmd_t msg_cmd VAR_RAM;
static msg_length_t msg_cmd_length VAR_RAM;
static enum command_state_t cmd_state VAR_RAM = STATE_IDLE;

static struct table_t *table_ctx VAR_RAM = NULL;

static struct font_t *get_font(const uint16_t font_id)
{
    struct font_t *font = NULL;
    switch (font_id) {
    case LPC_FONT_DEJAVU_SANS_8PX:
    {
        font = &dejavu_sans_8px;
        break;
    }
    case LPC_FONT_DEJAVU_SANS_12PX:
    {
        font = &dejavu_sans_12px;
        break;
    }
    case LPC_FONT_DEJAVU_SANS_12PX_BF:
    {
        font = &dejavu_sans_12px_bf;
        break;
    }
    case LPC_FONT_DEJAVU_SANS_20PX_BF:
    {
        font = &dejavu_sans_20px_bf;
        break;
    }
    default:
    {
        font = &dejavu_sans_8px;
        break;
    }
    }
    return font;
}

static inline void handle_idle_command()
{
    switch (msg_cmd.cmd) {
    case LPC_CMD_DRAW_RECT:
    {
        lcd_enable();
        draw_rectangle(
            msg_cmd.args.draw_rect.x0,
            msg_cmd.args.draw_rect.y0,
            msg_cmd.args.draw_rect.x1,
            msg_cmd.args.draw_rect.y1,
            msg_cmd.args.draw_rect.colour);
        lcd_disable();
        break;
    }
    case LPC_CMD_FILL_RECT:
    {
        lcd_enable();
        fill_rectangle(
            msg_cmd.args.draw_rect.x0,
            msg_cmd.args.draw_rect.y0,
            msg_cmd.args.draw_rect.x1,
            msg_cmd.args.draw_rect.y1,
            msg_cmd.args.draw_rect.colour);
        lcd_disable();
        break;
    }
    case LPC_CMD_DRAW_TEXT:
    {
        lcd_enable();
        font_draw_text(
            get_font(msg_cmd.args.draw_text.font),
            msg_cmd.args.draw_text.x0,
            msg_cmd.args.draw_text.y0,
            msg_cmd.args.draw_text.fgcolour,
            msg_cmd.args.draw_text.text);
        lcd_disable();
        break;
    }
    case LPC_CMD_DRAW_IMAGE_START:
    {
        lcd_enable();
        // intentionally not disabling LCD here
        lcd_setarea(
            msg_cmd.args.draw_image_start.x0,
            msg_cmd.args.draw_image_start.y0,
            msg_cmd.args.draw_image_start.x1,
            msg_cmd.args.draw_image_start.y1);
        lcd_drawstart();
        cmd_state = STATE_DRAWING_IMAGE;
        break;
    }
    case LPC_CMD_TABLE_START:
    {
        table_ctx = buffer_alloc(sizeof(struct table_t));
        struct table_column_t *columns = buffer_alloc(
            sizeof(struct table_column_t)*msg_cmd.args.table_start.column_count);
        if (!table_ctx || !columns) {
            comm_tx_nak(MSG_ADDRESS_HOST, MSG_NAK_OUT_OF_MEMORY);
            buffer_release_all();
            break;
        }

        for (int i = 0;
            i < msg_cmd.args.table_start.column_count;
            i++)
        {
            columns[i].width = msg_cmd.args.table_start.columns[i].width;
            columns[i].alignment = msg_cmd.args.table_start.columns[i].alignment;
        }

        table_init(
            table_ctx,
            columns,
            msg_cmd.args.table_start.column_count,
            msg_cmd.args.table_start.row_height);

        table_start(
            table_ctx,
            msg_cmd.args.table_start.x0,
            msg_cmd.args.table_start.y0);

        cmd_state = STATE_TABLE;
        break;
    }
    case LPC_CMD_DRAW_LINE:
    {
        lcd_enable();
        draw_line(
            msg_cmd.args.draw_line.x0,
            msg_cmd.args.draw_line.y0,
            msg_cmd.args.draw_line.x1,
            msg_cmd.args.draw_line.y1,
            msg_cmd.args.draw_line.colour);
        lcd_disable();
        break;
    }
    case LPC_CMD_DRAW_IMAGE_DATA:
    case LPC_CMD_DRAW_IMAGE_END:
    case LPC_CMD_TABLE_ROW:
    case LPC_CMD_TABLE_END:
    {
        comm_tx_nak(MSG_ADDRESS_HOST, MSG_NAK_CODE_ORDER);
        break;
    }
    default:
    {
        comm_tx_nak(MSG_ADDRESS_HOST, MSG_NAK_CODE_UNKNOWN_COMMAND);
        break;
    }
    }
}

static inline void handle_draw_image_command()
{
    switch (msg_cmd.cmd) {
    case LPC_CMD_DRAW_IMAGE_DATA:
    {
        const int16_t pixels = (msg_cmd_length - sizeof(struct lpc_cmd_draw_image_data_t) - sizeof(lpc_cmd_id_t)) / 2;
        const uint16_t *pixel_ptr = &msg_cmd.args.draw_image_data.pixels[0];
        //~ lcd_draw(*pixel_ptr);
        const uint16_t *pixel_end = pixel_ptr + pixels;
        while (pixel_ptr != pixel_end) {
            lcd_draw(*pixel_ptr++);
        }
        break;
    }
    case LPC_CMD_DRAW_IMAGE_END:
    {
        lcd_disable();
        cmd_state = STATE_IDLE;
        break;
    }
    default:
    {
        comm_tx_nak(MSG_ADDRESS_HOST, MSG_NAK_CODE_ORDER);
        break;
    }
    }
}

static inline void handle_table_command()
{
    switch (msg_cmd.cmd) {
    case LPC_CMD_TABLE_ROW:
    {
        lcd_enable();
        table_row_onebuffer(
            table_ctx,
            get_font(msg_cmd.args.table_row.font),
            &msg_cmd.args.table_row.contents[0],
            msg_cmd.args.table_row.fgcolour,
            msg_cmd.args.table_row.bgcolour);
        lcd_disable();
        break;
    }
    case LPC_CMD_TABLE_END:
    {
        buffer_release_all();
        cmd_state = STATE_IDLE;
        break;
    }
    default:
    {
        comm_tx_nak(MSG_ADDRESS_HOST, MSG_NAK_CODE_ORDER);
        break;
    }
    }
}

static inline void handle_command()
{
    if (msg_cmd.cmd == LPC_CMD_RESET_STATE) {
        cmd_state = STATE_IDLE;
        lcd_disable();
        buffer_release_all();
        return;
    }

    switch (cmd_state) {
    case STATE_IDLE:
    {
        handle_idle_command();
        break;
    }
    case STATE_DRAWING_IMAGE:
    {
        handle_draw_image_command();
        break;
    }
    case STATE_TABLE:
    {
        handle_table_command();
        break;
    }
    }

}

int main(void)
{
    struct msg_header_t msg_header = {0};
    HDR_SET_SENDER(msg_header, MSG_ADDRESS_LPC1114);
    HDR_SET_RECIPIENT(msg_header, MSG_ADDRESS_HOST);
    struct lpc_msg_t msg_payload;
    msg_checksum_t msg_checksum;
    coord_int_t prevx = -1;
    coord_int_t prevy = -1;

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
    TMR_TMR16B1MR0  = 0xC000;
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

    _Static_assert(sizeof(unsigned int) == 4, "foo");
    {
        font_draw_text(&dejavu_sans_12px,
            20, 40, 0xffff,
            (utf8_cstr_t)"Calibration");
        font_draw_text(&dejavu_sans_12px,
            20, 60, 0xffff,
            (utf8_cstr_t)"Please touch the highlighted points");

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

        //~ show_coords(buffer, x, y, z);

        fill_rectangle(LCD_WIDTH-6, LCD_HEIGHT-6, LCD_WIDTH-1, LCD_HEIGHT-1, 0xffff);
        lcd2.x = fp11_4_from_int16_t(LCD_WIDTH-3) + FP11_4_ZERO_POINT_FIVE;
        lcd2.y = fp11_4_from_int16_t(LCD_HEIGHT-3) + FP11_4_ZERO_POINT_FIVE;
        touch_wait_for_raw(&x, &y, &z);
        touch_wait_for_clear();
        touch2.x = fp11_4_from_int16_t(x);
        touch2.y = fp11_4_from_int16_t(y);
        fill_rectangle(LCD_WIDTH-6, LCD_HEIGHT-6, LCD_WIDTH-1, LCD_HEIGHT-1, 0x0000);

        //~ show_coords(buffer, x, y, z);

        touch_calculate_calibration(&lcd1, &lcd2, &touch1, &touch2, 0);

        touch_wait_for_clear();
    }

    fill_rectangle(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1, 0x0000);

    lcd_disable();

    while (1)
    {
        switch (wait_for_event())
        {
        case EV_COMM:
        {
            struct msg_buffer_t *msg = comm_get_rx_message();
            msg_cmd_length = HDR_GET_PAYLOAD_LENGTH(msg->msg.header);
            memcpy(&msg_cmd, &msg->msg.data[0], HDR_GET_PAYLOAD_LENGTH(msg->msg.header));
            bool send_ack = (HDR_GET_SENDER(msg->msg.header) == MSG_ADDRESS_HOST);
            comm_release_rx_message();

            if (send_ack) {
                comm_tx_ack(MSG_ADDRESS_HOST);
            }

            handle_command();
            break;
        }
        case EV_TOUCH:
        {
            coord_int_t x, y, z;
            x = touch_get_x();
            y = touch_get_y();
            z = touch_get_z();

            if ((z > 0) && (abs(prevx - x) + abs(prevy - y) <= 3)) {
                prevx = x;
                prevy = y;
                break;
            }

            prevx = x;
            prevy = y;

            if (z == 0) {
                prevx = -100;
                prevy = -100;
            }

            //~ lcd_enable();
            //~ fill_rectangle(x-2, y-2, x+2, y+2, 0x001f);
            //~ lcd_disable();

            msg_payload.subject = LPC_SUBJECT_TOUCH_EVENT;
            msg_payload.payload.touch_ev.x = x;
            msg_payload.payload.touch_ev.y = y;
            msg_payload.payload.touch_ev.z = z;
            HDR_SET_PAYLOAD_LENGTH(msg_header, sizeof(struct lpc_msg_t));
            msg_checksum = checksum((const uint8_t*)&msg_payload,
                                    sizeof(struct lpc_msg_t));

            comm_tx_message(&msg_header,
                            (const uint8_t*)&msg_payload,
                            msg_checksum);

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
