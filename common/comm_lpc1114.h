#ifndef _COMMON_LPC1114_H
#define _COMMON_LPC1114_H

#include "comm.h"
#include "types.h"

#define LPC_SUBJECT_TOUCH_EVENT (1)

struct __attribute__((packed)) lpc_msg_t {
    uint8_t subject;
    union {
        struct {
            uint16_t x, y;
            uint16_t z;
        } touch_ev __attribute__((packed));
    } payload;
};

struct __attribute__((packed)) lpc_cmd_draw_rect_t {
    uint16_t colour;
    int16_t x0, y0, x1, y1;
};

struct __attribute__((packed)) lpc_cmd_draw_line_t {
    uint16_t colour;
    int16_t x0, y0, x1, y1;
};

struct __attribute__((packed)) lpc_cmd_draw_image_start_t {
    int16_t x0, y0, x1, y1;
};

#define IMAGE_DATA_CHUNK_LENGTH ((MSG_MAX_PAYLOAD-sizeof(lpc_cmd_id_t))/2)
#define TEXT_LENGTH (MSG_MAX_PAYLOAD-(sizeof(lpc_cmd_id_t)+sizeof(uint16_t)*3+sizeof(uint8_t)))

struct __attribute__((packed)) lpc_cmd_draw_image_data_t {
    uint16_t pixels[0];
};

struct __attribute__((packed)) lpc_cmd_draw_text {
    uint16_t fgcolour;
    uint8_t font;
    int16_t x0, y0;
    uint8_t text[0];
};

struct __attribute__((packed)) lpc_cmd_table_start_t {
    uint16_t column_count;
    int16_t x0, y0;
    int16_t row_height;
    struct table_column_t columns[0];
};

struct __attribute__((packed)) lpc_cmd_table_row_t {
    uint16_t fgcolour;
    uint16_t bgcolour;
    uint8_t font;
    uint8_t contents[0];
};

struct __attribute__((packed)) lpc_cmd_set_brightness_t {
    uint16_t brightness;
};

#define LPC_CMD_FILL_RECT               (0x01)
#define LPC_CMD_DRAW_RECT               (0x02)
#define LPC_CMD_DRAW_IMAGE_START        (0x03)
#define LPC_CMD_DRAW_IMAGE_DATA         (0x04)
#define LPC_CMD_DRAW_IMAGE_END          (0x05)
#define LPC_CMD_RESET_STATE             (0x06)
#define LPC_CMD_DRAW_TEXT               (0x07)
#define LPC_CMD_TABLE_START             (0x08)
#define LPC_CMD_TABLE_ROW               (0x09)
#define LPC_CMD_TABLE_END               (0x0A)
#define LPC_CMD_DRAW_LINE               (0x0B)
#define LPC_CMD_SET_BRIGHTNESS          (0x0C)
#define LPC_CMD_LULLABY                 (0x0D)
#define LPC_CMD_WAKE_UP                 (0x0E)

#define LPC_FONT_DEJAVU_SANS_8PX        (0x10)
#define LPC_FONT_DEJAVU_SANS_12PX       (0x20)
#define LPC_FONT_DEJAVU_SANS_12PX_BF    (0x21)
#define LPC_FONT_CANTARELL_20PX_BF      (0x31)
#define LPC_FONT_DEJAVU_SANS_40PX       (0x40)

struct __attribute__((packed)) lpc_cmd_t {
    lpc_cmd_id_t cmd;
    union {
        struct lpc_cmd_draw_rect_t fill_rect;
        struct lpc_cmd_draw_rect_t draw_rect;
        struct lpc_cmd_draw_line_t draw_line;
        struct lpc_cmd_draw_image_start_t draw_image_start;
        struct lpc_cmd_draw_image_data_t draw_image_data;
        struct lpc_cmd_draw_text draw_text;
        struct lpc_cmd_table_start_t table_start;
        struct lpc_cmd_table_row_t table_row;
        struct lpc_cmd_set_brightness_t set_brightness;
        uint8_t raw[MSG_MAX_PAYLOAD-sizeof(lpc_cmd_id_t)];
    } args;
};

struct __attribute__((packed)) lpc_cmd_msg_t {
    struct msg_header_t header;
    struct lpc_cmd_t payload;
};

#if __STDC_VERSION__ >= 201112L

_Static_assert(sizeof(struct lpc_cmd_t) <= MSG_MAX_PAYLOAD,
               "lpc_cmd_t grew larger than maximum payload");

#endif

#endif
