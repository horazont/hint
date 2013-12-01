#include "lpcdisplay.h"

#include "common/comm_lpc1114.h"

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
    msg->payload.cmd = LPC_CMD_DRAW_LINE;
    msg->payload.args.draw_line.x0 = x0;
    msg->payload.args.draw_line.y0 = y0;
    msg->payload.args.draw_line.x1 = x1;
    msg->payload.args.draw_line.y1 = y1;
    msg->payload.args.draw_line.colour = colour;

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
    msg->payload.cmd = LPC_CMD_DRAW_RECT;
    msg->payload.args.draw_rect.x0 = x0;
    msg->payload.args.draw_rect.y0 = y0;
    msg->payload.args.draw_rect.x1 = x1;
    msg->payload.args.draw_rect.y1 = y1;
    msg->payload.args.draw_rect.colour = colour;

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
    msg->payload.cmd = LPC_CMD_DRAW_TEXT;
    msg->payload.args.draw_text.fgcolour = colour;
    msg->payload.args.draw_text.font = font;
    msg->payload.args.draw_text.x0 = x0;
    msg->payload.args.draw_text.y0 = y0;
    memcpy(&msg->payload.args.draw_text.text[0], text, text_buflen);

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
    msg->payload.cmd = LPC_CMD_FILL_RECT;
    msg->payload.args.fill_rect.x0 = x0;
    msg->payload.args.fill_rect.y0 = y0;
    msg->payload.args.fill_rect.x1 = x1;
    msg->payload.args.fill_rect.y1 = y1;
    msg->payload.args.fill_rect.colour = colour;

    comm_enqueue_msg(comm, msg);
}

void lpcd_table_end(
    struct comm_t *comm)
{
    const int payload_length = sizeof(lpc_cmd_id_t);
    struct lpc_cmd_msg_t *msg = comm_alloc_message(
        MSG_ADDRESS_LPC1114, payload_length);
    msg->payload.cmd = LPC_CMD_TABLE_END;

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
    msg->payload.cmd = LPC_CMD_TABLE_ROW;
    msg->payload.args.table_row.font = font;
    msg->payload.args.table_row.fgcolour = fgcolour;
    msg->payload.args.table_row.bgcolour = bgcolour;
    memcpy(
        &msg->payload.args.table_row.contents[0],
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
    msg->payload.cmd = LPC_CMD_TABLE_START;
    msg->payload.args.table_start.x0 = x0;
    msg->payload.args.table_start.y0 = y0;
    msg->payload.args.table_start.row_height = row_height;
    msg->payload.args.table_start.column_count = column_count;
    memcpy(
        &msg->payload.args.table_start.columns[0],
        &columns[0],
        sizeof(struct table_column_t)*column_count);

    comm_dump_message((struct msg_header_t*)msg);
    comm_enqueue_msg(comm, msg);
}

