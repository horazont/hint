#include "lpcdisplay.h"

#include "common/comm_lpc1114.h"

#include <alloca.h>
#include <string.h>

void lpcd_draw_line(
    struct comm_t *comm,
    const coord_int_t x0,
    const coord_int_t y0,
    const coord_int_t x1,
    const coord_int_t y1,
    const colour_t colour)
{
    const int payload_length = sizeof(lpc_cmd_id_t) +
                               sizeof(struct lpc_cmd_draw_line_t);
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = htole16(LPC_CMD_DRAW_LINE);
    msg->payload.args.draw_line.x0 = htole16(x0);
    msg->payload.args.draw_line.y0 = htole16(y0);
    msg->payload.args.draw_line.x1 = htole16(x1);
    msg->payload.args.draw_line.y1 = htole16(y1);
    msg->payload.args.draw_line.colour = htole16(colour);

    comm_enqueue_msg(comm, msg);
}

void lpcd_draw_rectangle(
    struct comm_t *comm,
    const coord_int_t x0,
    const coord_int_t y0,
    const coord_int_t x1,
    const coord_int_t y1,
    const colour_t colour)
{
    const int payload_length = sizeof(lpc_cmd_id_t) +
                               sizeof(struct lpc_cmd_draw_rect_t);
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = htole16(LPC_CMD_DRAW_RECT);
    msg->payload.args.draw_rect.x0 = htole16(x0);
    msg->payload.args.draw_rect.y0 = htole16(y0);
    msg->payload.args.draw_rect.x1 = htole16(x1);
    msg->payload.args.draw_rect.y1 = htole16(y1);
    msg->payload.args.draw_rect.colour = htole16(colour);

    comm_enqueue_msg(comm, msg);
}

void lpcd_draw_text(
    struct comm_t *comm,
    const coord_int_t x0,
    const coord_int_t y0,
    const int font,
    const colour_t colour,
    const char *text)
{
    const int text_buflen = strlen(text)+1;
    const int payload_length = sizeof(lpc_cmd_id_t) +
                               sizeof(struct lpc_cmd_draw_text) +
                               text_buflen;
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = htole16(LPC_CMD_DRAW_TEXT);
    msg->payload.args.draw_text.fgcolour = htole16(colour);
    msg->payload.args.draw_text.font = font;
    msg->payload.args.draw_text.x0 = htole16(x0);
    msg->payload.args.draw_text.y0 = htole16(y0);
    memcpy(&msg->payload.args.draw_text.text[0], text, text_buflen);

    comm_enqueue_msg(comm, msg);
}

void lpcd_image_start(
    struct comm_t *comm,
    const coord_int_t x0,
    const coord_int_t y0,
    const coord_int_t x1,
    const coord_int_t y1)
{
    const int payload_length = sizeof(lpc_cmd_id_t) +
        sizeof(struct lpc_cmd_draw_image_start_t);

    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);

    msg->payload.cmd = htole16(LPC_CMD_DRAW_IMAGE_START);
    msg->payload.args.draw_image_start.x0 = htole16(x0);
    msg->payload.args.draw_image_start.x1 = htole16(x1);
    msg->payload.args.draw_image_start.y0 = htole16(y0);
    msg->payload.args.draw_image_start.y1 = htole16(y1);

    comm_enqueue_msg(comm, msg);
}

void lpcd_image_data(
    struct comm_t *comm,
    const void *buffer,
    const size_t length)
{
    const int payload_length = sizeof(lpc_cmd_id_t) +
        length;

    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);

    msg->payload.cmd = htole16(LPC_CMD_DRAW_IMAGE_DATA);
    memcpy(&msg->payload.args.draw_image_data.pixels[0],
           buffer,
           length);

    comm_enqueue_msg(comm, msg);
}

void lpcd_fill_rectangle(
    struct comm_t *comm,
    const coord_int_t x0,
    const coord_int_t y0,
    const coord_int_t x1,
    const coord_int_t y1,
    const colour_t colour)
{
    const int payload_length = sizeof(lpc_cmd_id_t) +
                               sizeof(struct lpc_cmd_draw_rect_t);
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = htole16(LPC_CMD_FILL_RECT);
    msg->payload.args.fill_rect.x0 = htole16(x0);
    msg->payload.args.fill_rect.y0 = htole16(y0);
    msg->payload.args.fill_rect.x1 = htole16(x1);
    msg->payload.args.fill_rect.y1 = htole16(y1);
    msg->payload.args.fill_rect.colour = colour;

    comm_enqueue_msg(comm, msg);
}

void lpcd_lullaby(
    struct comm_t *comm)
{
    const int payload_length = sizeof(lpc_cmd_id_t);
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = htole16(LPC_CMD_LULLABY);

    comm_enqueue_msg(comm, msg);
}

void lpcd_table_row(
    struct comm_t *comm,
    const int font,
    const colour_t fgcolour,
    const colour_t bgcolour,
    const char *columns,
    const int columns_len)
{
    const int payload_length =
        sizeof(lpc_cmd_id_t) +
        sizeof(struct lpc_cmd_table_row_t) +
        columns_len;
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = htole16(LPC_CMD_TABLE_ROW);
    msg->payload.args.table_row.font = font;
    msg->payload.args.table_row.fgcolour = htole16(fgcolour);
    msg->payload.args.table_row.bgcolour = htole16(bgcolour);
    memcpy(
        &msg->payload.args.table_row.contents[0],
        columns,
        columns_len);

    comm_enqueue_msg(comm, msg);
}

void lpcd_table_row_ex(
    struct comm_t *comm,
    const int font,
    const struct table_column_ex_t *columns,
    const int columns_len)
{
    const int payload_length =
        sizeof(lpc_cmd_id_t) +
        sizeof(struct lpc_cmd_table_row_t) +
        columns_len;
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = htole16(LPC_CMD_TABLE_ROW_EX);
    msg->payload.args.table_row_ex.font = font;
    memcpy(
        &msg->payload.args.table_row_ex.contents[0],
        columns,
        columns_len);

    comm_enqueue_msg(comm, msg);
}

void lpcd_table_start(
    struct comm_t *comm,
    const coord_int_t x0,
    const coord_int_t y0,
    const coord_int_t row_height,
    const struct table_column_t columns[],
    const int column_count)
{
    const int payload_length =
        sizeof(lpc_cmd_id_t) +
        sizeof(struct lpc_cmd_table_start_t) +
        sizeof(struct table_column_t) * column_count;
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = htole16(LPC_CMD_TABLE_START);
    msg->payload.args.table_start.x0 = htole16(x0);
    msg->payload.args.table_start.y0 = htole16(y0);
    msg->payload.args.table_start.row_height = htole16(row_height);
    msg->payload.args.table_start.column_count = htole16(column_count);

    struct table_column_t *columns_encoded =
        alloca(sizeof(struct table_column_t)*column_count);
    for (int i = 0; i < column_count; i++) {
        columns_encoded[i].width = htole16(columns[i].width);
        columns_encoded[i].alignment = columns[i].alignment;

    }

    memcpy(
        &msg->payload.args.table_start.columns[0],
        &columns_encoded[0],
        sizeof(struct table_column_t)*column_count);

    comm_enqueue_msg(comm, msg);
}

void lpcd_set_brightness(
    struct comm_t *comm,
    const uint16_t brightness)
{
    const int payload_length =
        sizeof(lpc_cmd_id_t) +
        sizeof(struct lpc_cmd_set_brightness_t);
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = htole16(LPC_CMD_SET_BRIGHTNESS);
    msg->payload.args.set_brightness.brightness = htole16(brightness);

    comm_enqueue_msg(comm, msg);
}

void lpcd_state_reset(struct comm_t *comm)
{
    const int payload_length =
        sizeof(lpc_cmd_id_t);
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = htole16(LPC_CMD_RESET_STATE);

    comm_enqueue_msg(comm, msg);
}

void lpcd_wake_up(
    struct comm_t *comm)
{
    const int payload_length = sizeof(lpc_cmd_id_t);
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = htole16(LPC_CMD_WAKE_UP);

    comm_enqueue_msg(comm, msg);
}
