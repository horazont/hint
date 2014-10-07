#include "uart_onewire.h"

#include "lcd.h"

#include <util/delay.h>

#define ONEWIRE_TXEN_SHIFT (DDB4)

#define wait_for_conversion() { _delay_ms(800); }

void init_uart()
{
    UCSRC = (1<<UCSZ1) | (1<<UCSZ0);
}

void set_to_controlbaud()
{
    // disable uart
    UCSRB = 0;

#define BAUD (9600L)
#include <util/setbaud.h>
    UBRRH = UBRRH_VALUE;
    UBRRL = UBRRL_VALUE;
#if USE_2X
    UCSRA |= (1 << U2X);
#else
    UCSRA &= ~(1 << U2X);
#endif
#undef BAUD
#undef UBRR_VALUE
#undef UBRRH_VALUE
#undef UBRRL_VALUE
#undef USE_2X

    UCSRB = (1<<RXEN) | (1<<TXEN);
}

void set_to_databaud()
{
    // disable uart
    UCSRB = 0;

#define BAUD (100000L)
#include <util/setbaud.h>
    UBRRH = UBRRH_VALUE;
    UBRRL = UBRRL_VALUE;
#if USE_2X
    UCSRA |= (1 << U2X);
#else
    UCSRA &= ~(1 << U2X);
#endif
#undef BAUD
#undef UBRR_VALUE
#undef UBRRH_VALUE
#undef UBRRL_VALUE
#undef USE_2X

    UCSRB = (1<<RXEN) | (1<<TXEN);
}

static inline uint8_t onewire_probe(const uint8_t signal)
{
    uart_tx_sync(signal);
    return uart_rx_sync();
}

static inline void onewire_write1()
{
    onewire_probe(0xFF);
}

static inline void onewire_write0()
{
    onewire_probe(0x00);
}

static inline uint8_t onewire_read()
{
    // startbit pulls the bus low, then keep the bus high
    return onewire_probe(0xFF) == 0xFF;
}

static inline void onewire_disable_pullup()
{
    PORTB |= (1<<ONEWIRE_TXEN_SHIFT);
}

static inline void onewire_enable_pullup()
{
    PORTB &= ~(1<<ONEWIRE_TXEN_SHIFT);
}

void onewire_write_byte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        _delay_us(10);
        if (byte & 0x1) {
            onewire_write1();
        } else {
            onewire_write0();
        }
        byte >>= 1;
    }
}

void onewire_write_byte_and_pullup(uint8_t byte)
{
    for (uint8_t i = 0; i < 7; i++) {
        _delay_us(10);
        if (byte & 0x1) {
            onewire_write1();
        } else {
            onewire_write0();
        }
        byte >>= 1;
    }
    _delay_us(10);
    if (byte & 0x1) {
        uart_tx_sync(0xFF);
    } else {
        uart_tx_sync(0x00);
    }
    // wait a few us so that the uart has a chance to start sending.
    _delay_us(15);
    // aaand pullup.
    onewire_enable_pullup();
    // and only now we wait for receiving the full frame.
    uart_rx_sync();
}

uint8_t onewire_read_byte()
{
    uint8_t result = 0x00;
    for (int8_t shift = 0; shift < 8; shift++)
    {
        if (onewire_read()) {
            result |= (1 << shift);
        }
    }
    return result;
}

void onewire_init()
{
    set_to_databaud();
    init_uart();
    onewire_disable_pullup();
}

uint8_t onewire_findnext(onewire_addr_t addr)
{
    uint8_t status = onewire_reset();
    if (status != UART_1W_PRESENCE) {
        return status | 0x40;
    }

    // this algorithm requires two scans over the whole address range to find
    // the next ROM address (which is then already selected)
    uint8_t previous_alternative_bit = 0xff;
    // initiate search
    onewire_write_byte(0xF0);

    for (uint_least8_t offs = 0; offs < UART_1W_ADDR_LEN*8; offs++)
    {
        const uint8_t false_presence = !onewire_read();
        const uint8_t true_presence = !onewire_read();
        (void)false_presence;


        const uint8_t byteaddr = (offs & 0xF8) >> 3;
        const uint8_t bitoffs = (offs & 0x07);

        uint8_t prevbit = addr[byteaddr] & (1<<bitoffs);

        /* lcd_writestate(false_presence, true_presence, prevbit); */

        if (!prevbit) {
            // note this as a possible position to return to on the second
            // iteration, as a higher value is possible here
            if (true_presence) {
                previous_alternative_bit = offs;
            }
            if (!false_presence) {
                // we can abort here; the device which possibly was at the
                // current address is not here anymore.
                break;
            }
        } else if (!true_presence) {
            // dito
            break;
        }

        if (prevbit) {
            onewire_write1();
        } else {
            onewire_write0();
        }
    }

    // we found no higher possible address
    if (previous_alternative_bit == 0xff) {
        return UART_1W_NO_MORE_DEVICES;
    }

    // re-initialize the bus for searching the next device
    status = onewire_reset();
    if (status != UART_1W_PRESENCE) {
        return status | 0x80;
    }

    onewire_write_byte(0xF0);

    for (uint_least8_t offs = 0; offs < previous_alternative_bit; offs++)
    {
        // ignore the presence strobes, although we *could* use them as a safety
        // net. if the device isn’t there anymore, we’ll find out later.
        onewire_read();
        onewire_read();
        const uint8_t byteaddr = (offs & 0xF8) >> 3;
        const uint8_t bitoffs = (offs & 0x07);

        if (addr[byteaddr] & (1<<bitoffs)) {
            onewire_write1();
        } else {
            onewire_write0();
        }
    }

    // okay, we have the equal part of the address, let’s continue with the new
    // part.

    // not looking for 'false' devices
    onewire_read();

    if (onewire_read()) {
        // the device we’re expecting is not here ...
        return UART_1W_ERROR;
    }

    // we’re definitely going down the 'true' route; we only have to set that
    // bit now ...
    onewire_write1();

    {
        const uint8_t byteaddr = (previous_alternative_bit & 0xF8) >> 3;
        const uint8_t bitoffs = (previous_alternative_bit & 0x07);
        addr[byteaddr] |= (1<<bitoffs);
    }

    for (uint_least8_t offs = previous_alternative_bit+1;
         offs < UART_1W_ADDR_LEN*8;
         offs++)
    {
        const uint8_t
            false_presence = !onewire_read(),
            true_presence = !onewire_read();

        /* lcd_writepresence(false_presence, */
        /*                   true_presence); */

        const uint8_t byteaddr = (offs & 0xF8) >> 3;
        const uint8_t bitoffs = (offs & 0x07);

        // we must go for false presence first (lower value)
        if (false_presence) {
            addr[byteaddr] &= ~(1<<bitoffs);
            onewire_write0();
        } else if (true_presence) {
            addr[byteaddr] |= (1<<bitoffs);
            onewire_write1();
        }  else {
            // device vanished
            return UART_1W_ERROR;
        }

    }

    return UART_1W_PRESENCE;
}

uint8_t onewire_address_device(const onewire_addr_t addr)
{
    uint8_t status = onewire_reset();
    if (status != UART_1W_PRESENCE) {
        return status;
    }
    // MATCH ROM
    onewire_write_byte(0x55);
    // write addr
    for (uint8_t i = 0; i < UART_1W_ADDR_LEN; i++) {
        onewire_write_byte(addr[i]);
    }

    return status;
}

void onewire_ds18b20_broadcast_conversion()
{
    onewire_reset();
    // skip over rom search (address all teh devices)
    onewire_write_byte(0xCC);
    onewire_write_byte(0x44);
    onewire_enable_pullup();
    wait_for_conversion();
    onewire_disable_pullup();
}

void onewire_ds18b20_invoke_conversion(
    const onewire_addr_t device)
{
    onewire_address_device(device);
    onewire_write_byte(0x44);
    onewire_enable_pullup();
    wait_for_conversion();
    onewire_disable_pullup();
}

void onewire_ds18b20_read_scratchpad(
    const onewire_addr_t device,
    uint8_t blob[9])
{
    onewire_address_device(device);
    onewire_write_byte(0xBE);
    for (uint8_t i = 0; i < 9; i++) {
        blob[i] = onewire_read_byte();
    }
}

int16_t onewire_ds18b20_read_temperature(
    const onewire_addr_t device)
{
    onewire_address_device(device);
    onewire_write_byte(0xBE);
    uint16_t temperature = 0x00;
    temperature |= onewire_read_byte();
    temperature |= (onewire_read_byte() << 8);
    return (int16_t)temperature;
}

static inline uint8_t onewire_control_probe(const uint8_t signal)
{
    set_to_controlbaud();
    _delay_ms(1);
    uart_tx_sync(signal);
    uint8_t response = uart_rx_sync();
    set_to_databaud();
    _delay_ms(1);
    return response;
}

uint8_t onewire_reset()
{
    uint8_t response = onewire_control_probe(0xF0);
    if (response & 0x0F) {
        return UART_1W_ERROR;
    }

    if ((response & 0xF0) < 0xF0) {
        return UART_1W_PRESENCE;
    }

    return UART_1W_EMPTY;
}
