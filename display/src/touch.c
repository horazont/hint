#include "touch.h"

#include <string.h>

#include "config.h"
#include "utils.h"

#define MIN_PRESSURE 200

#define IOCON_PIO                      (0x00<<0) //pio
#define IOCON_R_PIO                    (0x01<<0) //pio (reserved pins)
#define IOCON_ADC                      (0x01<<0) //adc
#define IOCON_R_ADC                    (0x02<<0) //adc (reserved pins)
#define IOCON_NOPULL                   (0x00<<3) //no pull-down/pull-up
#define IOCON_PULLDOWN                 (0x01<<3) //pull-down
#define IOCON_PULLUP                   (0x02<<3) //pull-up
#define IOCON_ANALOG                   (0x00<<7) //analog (adc pins)
#define IOCON_DIGITAL                  (0x01<<7) //digital (adc pins)

#define XP_IOCON            IOCON_JTAG_TDI_PIO0_11
#define XP_IOCON_AD         IOCON_JTAG_TDI_PIO0_11_FUNC_AD0
#define XP_IOCON_GPIO       IOCON_JTAG_TDI_PIO0_11_FUNC_GPIO
#define XP_IOCON_DIGITAL    IOCON_JTAG_TDI_PIO0_11_ADMODE_DIGITAL
#define XP_IOCON_ANALOG     IOCON_JTAG_TDI_PIO0_11_ADMODE_ANALOG
#define XP_IOCON_PULLUP     IOCON_JTAG_TDI_PIO0_11_MODE_PULLUP
#define XP_IOCON_PULLDOWN   IOCON_JTAG_TDI_PIO0_11_MODE_PULLDOWN
#define XP_IOCON_INACTIVE   IOCON_JTAG_TDI_PIO0_11_MODE_INACTIVE
#define XP_PORT             (0)
#define XP_PIN              (11)
#define XP_AD               (0)

#define XM_IOCON            IOCON_JTAG_TDO_PIO1_1
#define XM_IOCON_AD         IOCON_JTAG_TDO_PIO1_1_FUNC_AD2
#define XM_IOCON_GPIO       IOCON_JTAG_TDO_PIO1_1_FUNC_GPIO
#define XM_IOCON_DIGITAL    IOCON_JTAG_TDO_PIO1_1_ADMODE_DIGITAL
#define XM_IOCON_ANALOG     IOCON_JTAG_TDO_PIO1_1_ADMODE_ANALOG
#define XM_IOCON_PULLUP     IOCON_JTAG_TDO_PIO1_1_MODE_PULLUP
#define XM_IOCON_PULLDOWN   IOCON_JTAG_TDO_PIO1_1_MODE_PULLDOWN
#define XM_IOCON_INACTIVE   IOCON_JTAG_TDO_PIO1_1_MODE_INACTIVE
#define XM_PORT             (1)
#define XM_PIN              (1)
#define XM_AD               (2)

#define YP_IOCON            IOCON_JTAG_TMS_PIO1_0
#define YP_IOCON_AD         IOCON_JTAG_TMS_PIO1_0_FUNC_AD1
#define YP_IOCON_GPIO       IOCON_JTAG_TMS_PIO1_0_FUNC_GPIO
#define YP_IOCON_DIGITAL    IOCON_JTAG_TMS_PIO1_0_ADMODE_DIGITAL
#define YP_IOCON_ANALOG     IOCON_JTAG_TMS_PIO1_0_ADMODE_ANALOG
#define YP_IOCON_PULLUP     IOCON_JTAG_TMS_PIO1_0_MODE_PULLUP
#define YP_IOCON_PULLDOWN   IOCON_JTAG_TMS_PIO1_0_MODE_PULLDOWN
#define YP_IOCON_INACTIVE   IOCON_JTAG_TMS_PIO1_0_MODE_INACTIVE
#define YP_PORT             (1)
#define YP_PIN              (0)
#define YP_AD               (1)

#define YM_IOCON            IOCON_JTAG_nTRST_PIO1_2
#define YM_IOCON_AD         IOCON_JTAG_nTRST_PIO1_2_FUNC_AD3
#define YM_IOCON_GPIO       IOCON_JTAG_nTRST_PIO1_2_FUNC_GPIO
#define YM_IOCON_DIGITAL    IOCON_JTAG_nTRST_PIO1_2_ADMODE_DIGITAL
#define YM_IOCON_ANALOG     IOCON_JTAG_nTRST_PIO1_2_ADMODE_ANALOG
#define YM_IOCON_PULLUP     IOCON_JTAG_nTRST_PIO1_2_MODE_PULLUP
#define YM_IOCON_PULLDOWN   IOCON_JTAG_nTRST_PIO1_2_MODE_PULLDOWN
#define YM_IOCON_INACTIVE   IOCON_JTAG_nTRST_PIO1_2_MODE_INACTIVE
#define YM_PORT             (1)
#define YM_PIN              (2)
#define YM_AD               (3)

#define GPIO_GPIOn_BASE(n) (GPIO_GPIO0_BASE | (n << 16))

_Static_assert(GPIO_GPIOn_BASE(1) == GPIO_GPIO1_BASE, "GPIOn_BASE does not work properly");
_Static_assert(GPIO_GPIOn_BASE(2) == GPIO_GPIO2_BASE, "GPIOn_BASE does not work properly");
_Static_assert(GPIO_GPIOn_BASE(3) == GPIO_GPIO3_BASE, "GPIOn_BASE does not work properly");

#define ANY_SET_DATA(iocon, port, pin, v) { \
    iocon = iocon##_GPIO | iocon##_INACTIVE | iocon##_DIGITAL; \
    *pREG32(GPIO_GPIOn_BASE(port) | 0x8000) |= (1<<pin); \
    *pREG32(GPIO_GPIOn_BASE(port) | ((1<<pin)<<2)) = v << pin; }

#define ANY_SET_HIZUP(iocon, port, pin) { \
    iocon = iocon##_GPIO | iocon##_PULLUP | iocon##_DIGITAL; \
    *pREG32(GPIO_GPIOn_BASE(port) | 0x8000) &= ~(1<<pin); \
    *pREG32(GPIO_GPIOn_BASE(port) | ((1<<pin)<<2)) = 0x000; }

#define ANY_SET_HIZDOWN(iocon, port, pin) { \
    iocon = iocon##_GPIO | iocon##_PULLDOWN | iocon##_DIGITAL; \
    *pREG32(GPIO_GPIOn_BASE(port) | 0x8000) &= ~(1<<pin); \
    *pREG32(GPIO_GPIOn_BASE(port) | ((1<<pin)<<2)) = 0x000; }

#define ANY_SET_ADC(iocon, port, pin) { \
    iocon = iocon##_AD | iocon##_INACTIVE | iocon##_ANALOG; \
    *pREG32(GPIO_GPIOn_BASE(port) | 0x8000) &= ~(1<<pin); \
    *pREG32(GPIO_GPIOn_BASE(port) | ((1<<pin)<<2)) = 0x000; }

#define DATA(which, v)  ANY_SET_DATA(which ## _IOCON, which ## _PORT, which ## _PIN, v)
#define HIZUP(which)    ANY_SET_HIZUP(which ## _IOCON, which ## _PORT, which ## _PIN)
#define HIZDOWN(which)  ANY_SET_HIZDOWN(which ## _IOCON, which ## _PORT, which ## _PIN)
#define ADC(which)      ANY_SET_ADC(which ## _IOCON, which ## _PORT, which ## _PIN)

static coord_int_t raw_x VAR_RAM;
static coord_int_t raw_y VAR_RAM;
static coord_int_t raw_z VAR_RAM;
static coord_int_t last_x VAR_RAM;
static coord_int_t last_y VAR_RAM;

/* static coord_int_t intr_tmp VAR_RAM; */

volatile int touch_ev VAR_RAM;
volatile enum touch_intr_state_t touch_intr_state VAR_RAM = TOUCH_STATE_IDLE;

static struct touch_calibration_t calibration VAR_RAM;

int touch_intr_start()
{
    if (touch_intr_state != TOUCH_STATE_IDLE)
    {
        return 1;
    }

    touch_intr_state = TOUCH_STATE_SAMPLING_Z;
    ADC(XM);
    ADC(YP);
    // set X+ to Vcc, Y- to GND
    DATA(XP, 1);
    DATA(YM, 0);

    *(pREG32(ADC_AD0INTEN)) |= ADC_AD0INTEN_ADINTEN2;
    ADC_AD0CR = ADC_AD0CR_BURST_HWSCANMODE
              | ADC_AD0CR_SEL_AD2
              | ADC_AD0CR_SEL_AD1
              | ADC_AD0CR_CLKS_10BITS
              | (ADC_AD0CR & ADC_AD0CR_CLKDIV_MASK);

    return 0;
}

void touch_intr_sm()
{
    // first, disable sampling
    *(pREG32(ADC_AD0INTEN)) = 0;
    ADC_AD0CR = ((~ADC_AD0CR_BURST_MASK) & ADC_AD0CR) | ADC_AD0CR_BURST_SWMODE;
    switch (touch_intr_state) {
    case TOUCH_STATE_IDLE:
    {
        // this is invalid!
        break;
    }
    case TOUCH_STATE_SAMPLING_Z:
    {
        coord_int_t z1, z2;
        z1 = *(pREG32(ADC_AD0DR1)) & 0x3FF;
        z2 = *(pREG32(ADC_AD0DR2)) & 0x3FF;

        raw_z = (0x3FF-z2) + z1;
        if (raw_z < MIN_PRESSURE) {
            raw_x = 0xfff;
            raw_y = 0xfff;
            touch_intr_state = TOUCH_STATE_IDLE;
            break;
        }

        ADC(YP);
        ADC(YM);
        DATA(XP, 1);
        DATA(XM, 0);

        *(pREG32(ADC_AD0INTEN)) |= ADC_AD0INTEN_ADINTEN3;
        ADC_AD0CR = ADC_AD0CR_BURST_HWSCANMODE
                  | ADC_AD0CR_SEL_AD3
                  | ADC_AD0CR_CLKS_10BITS
                  | (ADC_AD0CR & ADC_AD0CR_CLKDIV_MASK);

        touch_intr_state = TOUCH_STATE_SAMPLING_X;
        break;
    }
    /*case TOUCH_STATE_SAMPLING_X1:
    {
        intr_tmp = ADC_AD0DR3 & 0x3FF;
        touch_intr_state = TOUCH_STATE_SAMPLING_X2;
        ADC_AD0CR = ((~ADC_AD0CR_BURST_MASK) & ADC_AD0CR) | ADC_AD0CR_BURST_HWSCANMODE;
        break;
    }
    case TOUCH_STATE_SAMPLING_X2:
    {
        coord_int_t x1 = intr_tmp;
        coord_int_t x2 = ADC_AD0DR3 & 0x3FF;

        raw_x = x1;

        touch_intr_state = TOUCH_STATE_SAMPLING_Y1;
        ADC_AD0CR = ((~ADC_AD0CR_BURST_MASK) & ADC_AD0CR) | ADC_AD0CR_BURST_HWSCANMODE;
        break;
    }*/
    case TOUCH_STATE_SAMPLING_X:
    {
        raw_x = *(pREG32(ADC_AD0DR3)) & 0x3FF;

        ADC(XP);
        ADC(XM);
        DATA(YP, 1);
        DATA(YM, 0);

        *(pREG32(ADC_AD0INTEN)) |= ADC_AD0INTEN_ADINTEN2;
        ADC_AD0CR = ADC_AD0CR_BURST_HWSCANMODE
                  | ADC_AD0CR_SEL_AD2
                  | ADC_AD0CR_CLKS_10BITS
                  | (ADC_AD0CR & ADC_AD0CR_CLKDIV_MASK);
        touch_intr_state = TOUCH_STATE_SAMPLING_Y;
        break;
    }
    case TOUCH_STATE_SAMPLING_Y:
    {
        raw_y = *(pREG32(ADC_AD0DR2)) & 0x3FF;
        touch_intr_state = TOUCH_STATE_IDLE;

        HIZUP(XP);
        HIZUP(XM);
        HIZUP(YP);
        HIZUP(YM);

        break;
    }
    }
}

void touch_init()
{
    raw_x = 0;
    raw_y = 0;
    raw_z = 0;
    last_x = 0;
    last_y = 0;

    HIZUP(XP);
    HIZUP(XM);
    HIZUP(YP);
    HIZUP(YM);
}

static inline void calculate_calibration_xy(
    const fp11_4_t lcd1,
    const fp11_4_t lcd2,
    const fp11_4_t touch1,
    const fp11_4_t touch2,
    fp11_4_t *offset,
    int16_t *scale)
{
    const int32_t delta_lcd_27_4 = lcd1 - lcd2;
    const int32_t delta_touch_27_4 = touch1 - touch2;
    *scale = (int16_t)((delta_lcd_27_4 << 15) / delta_touch_27_4);

    const int32_t temp_11_19 = *scale * touch1;
    *offset = lcd1 - (fp11_4_t)(temp_11_19 >> 15);
}

void touch_calculate_calibration(
    const struct point11_4_t *lcd1,
    const struct point11_4_t *lcd2,
    const struct point11_4_t *touch1,
    const struct point11_4_t *touch2,
    int merge)
{
    struct touch_calibration_t new_calibration;

    /* until we find out which fixed-point setup is the best here, we'll
     * just do it by hand. */

    /* INPUT: ±11.4 */
    /* calibration.offset_{x,y}: ±11.4 */
    /* calibration.scale_{x,y}: ±0.15 */

    calculate_calibration_xy(
        lcd1->x, lcd2->x, touch1->x, touch2->x,
        &new_calibration.offset_x, &new_calibration.scale_x);
    calculate_calibration_xy(
        lcd1->y, lcd2->y, touch1->y, touch2->y,
        &new_calibration.offset_y, &new_calibration.scale_y);

    if (merge) {
        calibration.offset_x = fp11_4_avg(
            calibration.offset_x, new_calibration.offset_x);
        calibration.offset_y = fp11_4_avg(
            calibration.offset_y, new_calibration.offset_y);

        calibration.scale_x =
            (calibration.scale_x >> 1) + (new_calibration.scale_x >> 1);
        calibration.scale_y =
            (calibration.scale_y >> 1) + (new_calibration.scale_y >> 1);
    } else {
        calibration.offset_x = new_calibration.offset_x;
        calibration.offset_y = new_calibration.offset_y;
        calibration.scale_x = new_calibration.scale_x;
        calibration.scale_y = new_calibration.scale_y;
    }

    //~ new_calibration.scale_x = fp11_4_div(
        //~ fp11_4_sub(lcd1->x, lcd2->x),
        //~ fp11_4_sub(touch1->x, touch2->x));
    //~ new_calibration.offset_x = fp11_4_sub(
        //~ lcd1->x, fp11_4_mul(calibration.scale_x, touch1->x));
//~
    //~ new_calibration.scale_y = fp11_4_div(
        //~ fp11_4_sub(lcd1->y, lcd2->y),
        //~ fp11_4_sub(touch1->y, touch2->y));
    //~ new_calibration.offset_y = fp11_4_sub(
        //~ lcd1->y, fp11_4_mul(calibration.scale_x, touch1->y));
//~
    //~ if (merge) {
        //~ calibration.offset_x = fp11_4_avg(
            //~ new_calibration.offset_x, calibration.offset_x);
        //~ calibration.offset_y = fp11_4_avg(
            //~ new_calibration.offset_y, calibration.offset_y);
        //~ calibration.scale_x = fp11_4_avg(
            //~ new_calibration.scale_x, calibration.scale_x);
        //~ calibration.scale_y = fp11_4_avg(
            //~ new_calibration.scale_y, calibration.scale_y);
    //~ } else {
        //~ calibration.offset_x = new_calibration.offset_x;
        //~ calibration.offset_y = new_calibration.offset_y;
        //~ calibration.scale_x = new_calibration.scale_x;
        //~ calibration.scale_y = new_calibration.scale_y;
    //~ }


}

void touch_get_calibration(struct touch_calibration_t *dest)
{
    dest->offset_x = calibration.offset_x;
    dest->offset_y = calibration.offset_y;
    dest->scale_x = calibration.scale_x;
    dest->scale_y = calibration.scale_y;
}

inline coord_int_t touch_to_lcd(
    const coord_int_t raw_coord,
    const fp11_4_t offset,
    const int16_t scale /* ±0.15 */)
{
    const int32_t temp_15_15 = raw_coord * scale;
    const fp11_4_t shifted = (fp11_4_t)(temp_15_15 >> 11) + offset;
    return (shifted >> 4);
}

coord_int_t touch_get_x()
{
    return touch_to_lcd(raw_x,
                        calibration.offset_x, calibration.scale_x);
}

coord_int_t touch_get_y()
{
    return touch_to_lcd(raw_y,
                        calibration.offset_y, calibration.scale_y);
}

coord_int_t touch_get_z()
{
    return raw_z;
}

coord_int_t touch_get_raw_x()
{
    return raw_x;
}

coord_int_t touch_get_raw_y()
{
    return raw_y;
}

coord_int_t touch_get_raw_z()
{
    return raw_z;
}

void touch_test()
{
    // set X-, Y+ to ADC mode
    ADC(XM);
    ADC(YP);
    // set X+ to Vcc, Y- to GND
    DATA(XP, 1);
    DATA(YM, 0);
}

void touch_sample()
{
    uint32_t adc_cr, x1, x2, y1, y2, z, z1, z2;

    //save adc settings
    adc_cr = ADC_AD0CR;

    // set X-, Y+ to ADC mode
    ADC(XM);
    ADC(YP);
    // set X+ to Vcc, Y- to GND
    DATA(XP, 1);
    DATA(YM, 0);

    ADC_READ(XM_AD, z1);
    ADC_READ(YP_AD, z2);

    z = (1023-z1) + z2;

    raw_z = z;

    if(z > MIN_PRESSURE) //valid touch?
    {
        //cal_z = 0;

        //save values
        // middle of two values
        //  raw_x = (x1+x2)/2;
        //  raw_y = (y1+y2)/2;
        //  cal_z = z;
        // accept only if both readings are equal

        //get x
        ADC(YP);
        ADC(YM);
        DATA(XP, 1);
        DATA(XM, 0);

        ADC_READ(YM_AD, x1); x1 &= 0x3FE; //remove last bit
        ADC_READ(YM_AD, x2); x2 &= 0x3FE; //remove last bit

        if (x1)
        //~ if(x1 && (x1 == x2))
        {
            //get y
            ADC(XP);
            ADC(XM);
            DATA(YP, 1);
            DATA(YM, 0);
            ADC_READ(XM_AD, y1); y1 &= 0x3FE; //remove last bit
            ADC_READ(XM_AD, y2); y2 &= 0x3FE; //remove last bit

            if (y1)
            //~ if(y1 && (y1 == y2))
            {
                // x and y are swapped
                raw_x = 1023-y1;
                raw_y = x1;
                raw_z = z;
            }
        }
    }
    else
    {
        raw_z = 0;
    }

    //stop adc
    ADC_AD0CR = adc_cr;

    //set standby
    HIZUP(XP);
    HIZUP(XM);
    HIZUP(YP);
    HIZUP(YM);

    return;
}

void touch_wait_for_raw(coord_int_t *x, coord_int_t *y, coord_int_t *z)
{
    while (1) {
        touch_sample();
        if (raw_z > 0) {
            break;
        }
        delay_ms(50);
    }

    *x = raw_x;
    *y = raw_y;
    *z = raw_z;
}

void touch_wait_for_clear()
{
    while (1) {
        touch_sample();
        if (raw_z == 0) {
            break;
        }
        delay_ms(50);
    }
}

