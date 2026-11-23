#ifndef HK_UART_LOGGER_CORE_H
#define HK_UART_LOGGER_CORE_H

#include <stddef.h>
#include <stdint.h>

enum { UART_LOGGER_HISTORY = 96, UART_LOGGER_PACKET = 8, UART_LOGGER_QUEUE = 64 };

typedef struct
{
    uint16_t seq;
    int16_t value;
} logger_sample_t;

typedef struct
{
    uint32_t accepted;
    uint32_t gaps;
    uint32_t crc_errors;
    uint32_t stale;
    uint32_t digest;
    uint16_t last_seq;
    int16_t last_value;
    int16_t history[UART_LOGGER_HISTORY];
    uint8_t packet[UART_LOGGER_PACKET];
    uint8_t used;
    uint8_t next;
    uint8_t count;
} logger_stream_t;

typedef struct
{
    uint16_t x, y, width, height, color;
} logger_rect_t;

void logger_stream_init(logger_stream_t *stream);
uint8_t logger_feed(logger_stream_t *stream, uint8_t byte, logger_sample_t *sample);
void logger_encode(uint8_t packet[UART_LOGGER_PACKET], logger_sample_t sample);
size_t logger_csv_row(char *row, size_t size, logger_sample_t sample);
uint8_t logger_rect_count(const logger_stream_t *stream);
logger_rect_t logger_rect(const logger_stream_t *stream, uint8_t index);

#endif
