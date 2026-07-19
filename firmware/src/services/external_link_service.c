#include "external_link_service.h"

#include <stdio.h>
#include <string.h>

#include "../core/hk_string.h"
#include "../hal/hal_external_link.h"
#include "../hal/hal_time.h"
#include "debug_console_service.h"
#include "external_link_protocol.h"
#include "settings_persistence.h"
#include "settings_service.h"
#include "vision_result_service.h"

#define LINK_RESULT_HEADER_SIZE 10U
#define LINK_ITEM_WIRE_SIZE 16U

static external_link_transport_t g_transport = EXTERNAL_LINK_UART;
static hk_link_stream_parser_t g_uart_parser;
static uint8_t g_i2c_rx[HK_LINK_MAX_FRAME];
static volatile uint16_t g_i2c_rx_length;
static volatile uint8_t g_i2c_stop_seen;
static uint8_t g_i2c_pending[HK_LINK_MAX_FRAME];
static volatile uint16_t g_i2c_pending_length;
static volatile uint8_t g_i2c_pending_ready;
static uint8_t g_i2c_tx[HK_LINK_MAX_FRAME];
static volatile uint16_t g_i2c_tx_length;
static volatile uint16_t g_i2c_tx_index;
static uint32_t g_rx_frames;
static uint32_t g_tx_frames;
static uint32_t g_bad_frames;
static uint32_t g_rx_bytes;
static uint32_t g_uart_baud = 115200U;

static void write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint16_t make_response(const hk_link_message_t *request, uint8_t *frame, size_t capacity)
{
    uint8_t payload[HK_LINK_MAX_PAYLOAD];
    uint16_t payload_length = 0U;
    uint8_t response_type;
    vision_result_snapshot_t results;

    if(request->payload_length != 0U && request->type != HK_LINK_PING)
    {
        response_type = HK_LINK_ERROR;
        payload[0] = 2U;
        payload_length = 1U;
    }
    else if(request->type == HK_LINK_GET_INFO)
    {
        response_type = HK_LINK_INFO;
        write_u16(payload, 0x000FU); /* UART, I2C, BLOCK and ARROW. */
        payload[2] = VISION_RESULT_MAX_ITEMS;
        payload[3] = (uint8_t)g_transport;
        payload[4] = EXTERNAL_LINK_I2C_ADDRESS;
        payload[5] = 0U;
        write_u32(payload + 6, g_uart_baud);
        payload_length = 10U;
    }
    else if(request->type == HK_LINK_GET_RESULTS)
    {
        response_type = HK_LINK_RESULTS;
        vision_result_snapshot(&results);
        write_u32(payload, results.frame_id);
        payload[4] = results.source;
        payload[5] = results.count;
        write_u16(payload + 6, results.width);
        write_u16(payload + 8, results.height);
        payload_length = LINK_RESULT_HEADER_SIZE;
        for(uint8_t i = 0U; i < results.count; i++)
        {
            const vision_result_item_t *item = &results.items[i];
            uint8_t *wire = payload + payload_length;
            wire[0] = item->kind;
            wire[1] = item->flags;
            write_u16(wire + 2, item->id);
            write_u16(wire + 4, item->x0);
            write_u16(wire + 6, item->y0);
            write_u16(wire + 8, item->x1);
            write_u16(wire + 10, item->y1);
            write_u16(wire + 12, item->confidence);
            write_u16(wire + 14, item->reserved);
            payload_length += LINK_ITEM_WIRE_SIZE;
        }
    }
    else if(request->type == HK_LINK_PING)
    {
        response_type = HK_LINK_PONG;
        payload_length = request->payload_length;
        if(payload_length)
            memcpy(payload, request->payload, payload_length);
    }
    else
    {
        response_type = HK_LINK_ERROR;
        payload[0] = 1U;
        payload_length = 1U;
    }

    return (uint16_t)hk_link_frame_encode(response_type, request->sequence, payload,
                                          payload_length, frame, capacity);
}

static void handle_uart_message(const hk_link_message_t *message)
{
    uint8_t response[HK_LINK_MAX_FRAME];
    uint16_t length = make_response(message, response, sizeof(response));
    g_rx_frames++;
    if(length)
    {
        /* Give half-duplex and software UART masters time to return from TX
           to RX before the first response byte. */
        hal_sleep_ms(1U);
        hal_external_uart_send(response, length);
        g_tx_frames++;
    }
}

static void i2c_receive(uint8_t byte)
{
    if(g_i2c_rx_length < sizeof(g_i2c_rx))
        g_i2c_rx[g_i2c_rx_length++] = byte;
}

static uint8_t i2c_transmit(void)
{
    uint16_t index = g_i2c_tx_index;
    if(index >= g_i2c_tx_length)
        return 0U;
    g_i2c_tx_index = (uint16_t)(index + 1U);
    return g_i2c_tx[index];
}

static void i2c_finish_write(void)
{
    uint16_t length = g_i2c_rx_length;
    if(g_i2c_stop_seen && length && !g_i2c_pending_ready)
    {
        memcpy(g_i2c_pending, g_i2c_rx, length);
        g_i2c_pending_length = length;
        g_i2c_pending_ready = 1U;
    }
    g_i2c_stop_seen = 0U;
    g_i2c_rx_length = 0U;
}

static void i2c_event(hal_external_i2c_event_t event)
{
    if(event == HAL_EXTERNAL_I2C_EVENT_START)
    {
        /* The SDK reports STOP before draining RX_FULL in the same IRQ.  A
           following START is therefore the first safe interrupt-side point
           at which the completed write contains its final byte. */
        i2c_finish_write();
        g_i2c_rx_length = 0U;
        return;
    }
    g_i2c_stop_seen = 1U;
}

static const hal_external_i2c_callbacks_t g_i2c_callbacks = {
    .receive = i2c_receive,
    .transmit = i2c_transmit,
    .event = i2c_event,
};

void external_link_service_init(external_link_transport_t transport)
{
    memset(g_i2c_rx, 0, sizeof(g_i2c_rx));
    memset(g_i2c_pending, 0, sizeof(g_i2c_pending));
    memset(g_i2c_tx, 0, sizeof(g_i2c_tx));
    g_i2c_rx_length = 0U;
    g_i2c_stop_seen = 0U;
    g_i2c_pending_length = 0U;
    g_i2c_pending_ready = 0U;
    g_i2c_tx_length = 0U;
    g_i2c_tx_index = 0U;
    g_uart_baud = settings_external_link_uart_baud();
    hk_link_stream_reset(&g_uart_parser);
    external_link_service_set_transport(transport);
}

void external_link_service_set_transport(external_link_transport_t transport)
{
    g_transport = transport == EXTERNAL_LINK_I2C ? EXTERNAL_LINK_I2C : EXTERNAL_LINK_UART;
    hk_link_stream_reset(&g_uart_parser);
    g_i2c_pending_ready = 0U;
    g_i2c_rx_length = 0U;
    g_i2c_stop_seen = 0U;
    g_i2c_tx_length = 0U;
    g_i2c_tx_index = 0U;
    if(g_transport == EXTERNAL_LINK_I2C)
        hal_external_i2c_init(EXTERNAL_LINK_I2C_ADDRESS, &g_i2c_callbacks);
    else
        hal_external_uart_init(g_uart_baud);
    if(g_transport == EXTERNAL_LINK_I2C)
        printf("[LINK] I2C slave 0x32 IO34/IO35\r\n");
    else
        printf("[LINK] UART1 %u IO34/IO35\r\n", (unsigned)g_uart_baud);
}

external_link_transport_t external_link_service_transport(void)
{
    return g_transport;
}

void external_link_service_set_uart_baud(uint32_t baud)
{
    if(baud != 9600U && baud != 115200U && baud != 1000000U)
        baud = 115200U;
    g_uart_baud = baud;
    hk_link_stream_reset(&g_uart_parser);
    if(g_transport == EXTERNAL_LINK_UART)
        hal_external_uart_init(g_uart_baud);
}

uint32_t external_link_service_uart_baud(void)
{
    return g_uart_baud;
}

void external_link_service_tick(void)
{
    if(g_transport == EXTERNAL_LINK_UART)
    {
        uint8_t data[32];
        size_t count;
        hk_link_message_t message;
        do
        {
            count = hal_external_uart_receive(data, sizeof(data));
            g_rx_bytes += (uint32_t)count;
            for(size_t i = 0; i < count; i++)
                if(hk_link_stream_feed(&g_uart_parser, data[i], &message))
                    handle_uart_message(&message);
        } while(count == sizeof(data));
        return;
    }

    if(g_i2c_stop_seen)
        i2c_finish_write();
    if(g_i2c_pending_ready)
    {
        hk_link_message_t message;
        uint16_t length = g_i2c_pending_length;
        if(hk_link_frame_decode(g_i2c_pending, length, &message))
        {
            g_rx_frames++;
            g_i2c_tx_length = 0U;
            g_i2c_tx_index = 0U;
            g_i2c_tx_length = make_response(&message, g_i2c_tx, sizeof(g_i2c_tx));
            if(g_i2c_tx_length)
                g_tx_frames++;
        }
        else
            g_bad_frames++;
        g_i2c_pending_ready = 0U;
    }
}

void external_link_service_format_info(char *line, size_t line_size)
{
    if(line && line_size)
        snprintf(line, line_size, "HKLINKINFO mode=%s pins=%s uart=%u i2c=0x32 bytes=%u rx=%u tx=%u bad=%u\r\n",
                 g_transport == EXTERNAL_LINK_I2C ? "I2C" : "UART",
                 "IO34/IO35",
                 (unsigned)g_uart_baud,
                 (unsigned)g_rx_bytes, (unsigned)g_rx_frames, (unsigned)g_tx_frames,
                 (unsigned)g_bad_frames);
}

uint8_t external_link_service_handle_debug_command(const char *command)
{
    char line[128];
    external_link_transport_t transport;
    external_link_uart_speed_t speed;

    if(str_eq_ci(command, "HKLINKINFO"))
    {
        external_link_service_format_info(line, sizeof(line));
        debug_console_write_text(line);
        return 1U;
    }
    if(str_eq_ci(command, "HKLINK9600"))
        speed = EXTERNAL_LINK_UART_SPEED_9600;
    else if(str_eq_ci(command, "HKLINK115200"))
        speed = EXTERNAL_LINK_UART_SPEED_115200;
    else if(str_eq_ci(command, "HKLINK1000000"))
        speed = EXTERNAL_LINK_UART_SPEED_1000000;
    else
        speed = EXTERNAL_LINK_UART_SPEED_COUNT;

    if(speed < EXTERNAL_LINK_UART_SPEED_COUNT)
    {
        settings_set_external_link_uart_speed(speed);
        settings_set_external_link_transport(EXTERNAL_LINK_UART);
        settings_mark_dirty(1U);
        external_link_service_set_uart_baud(settings_external_link_uart_baud());
        external_link_service_set_transport(EXTERNAL_LINK_UART);
        external_link_service_format_info(line, sizeof(line));
        debug_console_write_text(line);
        return 1U;
    }

    if(str_eq_ci(command, "HKLINKUART"))
        transport = EXTERNAL_LINK_UART;
    else if(str_eq_ci(command, "HKLINKI2C"))
        transport = EXTERNAL_LINK_I2C;
    else
        return 0U;

    settings_set_external_link_transport(transport);
    settings_mark_dirty(1U);
    external_link_service_set_transport(transport);
    external_link_service_format_info(line, sizeof(line));
    debug_console_write_text(line);
    return 1U;
}
