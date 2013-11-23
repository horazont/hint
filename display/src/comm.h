#ifndef _HOSTCOM_H
#define _HOSTCOM_H

#include "common/comm.h"

#define COMM_ERR_NONE 0
#define COMM_ERR_NO_BACKBUFFER_AVAILABLE 1
#define COMM_ERR_NO_ROUTEBUFFER_AVAILABLE 2
#define COMM_ERR_UNKNOWN_RECIPIENT 3

/**
 * Initialize the communication subsystem.
 *
 * This initializes the UART (and will later also initialize the SPI).
 *
 * @param uart_baudrate baudrate to communicate with over the UART.
 */
void comm_init(const uint32_t uart_baudrate);

/**
 * Return received message. This should be called from the RX interrupt
 * handler.
 *
 * @return NULL if no message is currently available, a pointer to the
 *         buffer otherwise.
 */
struct msg_buffer_t *comm_get_rx_message();

/**
 * Release the received message. You MUST call this after the processing
 * of the message has finished. Otherwise, no further messages can be
 * received.
 *
 * @return true if another message is waiting to be processed, false
 *         otherwise.
 */
bool comm_release_rx_message();

/**
 * Transmit an acknowledge message to the given recipient.
 *
 * @param recipient target adress to send to
 * @return status code indicating success or failure of the transmission
 */
enum msg_status_t comm_tx_ack(const msg_address_t recipient);

/**
 * Transmit an not-acknowledged message to the given recipient.
 *
 * @param recipient target adress to send to
 * @param nak_code error code (4 bits)
 * @return status code indicating success or failure of the transmission
 */
enum msg_status_t comm_tx_nak(
    const msg_address_t recipient,
    const uint8_t nak_code);

/**
 * Transmit a message over the appropriate link. The link is detected
 * by investigating the recipient field in the header.
 *
 * payload may be NULL. In that case, no payload is transmitted
 * (checksum is ignored too) and payload_length in hdr MUST be 0 too.
 *
 * @param hdr message header
 * @param payload pointer to a buffer containing the payload
 * @param checksum checksum of the buffer
 * @return status code indicating success or failure of the transmission
 */
enum msg_status_t comm_tx_message(
    const struct msg_header_t *hdr,
    const uint8_t *payload,
    const msg_checksum_t checksum);

#endif
