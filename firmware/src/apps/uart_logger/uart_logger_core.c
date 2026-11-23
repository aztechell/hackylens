#include "uart_logger_core.h"

#include <stdio.h>
#include <string.h>

#define UART_LOGGER_SYNC_0 0xa5U
#define UART_LOGGER_SYNC_1 0x5aU

void logger_stream_init(logger_stream_t *stream)
{
    memset(stream, 0, sizeof(*stream));
    stream->digest = UINT32_C(2166136261);
}

static uint16_t decode_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint16_t packet_crc(const uint8_t *data)
{
    uint16_t crc = 0xffffU;
    for(unsigned i = 0U; i < 4U; ++i)
    {
        crc ^= (uint16_t)data[i] << 8U;
        for(unsigned bit = 0U; bit < 8U; ++bit)
            crc = (uint16_t)((crc << 1U) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
    }
    return crc;
}

void logger_encode(uint8_t packet[UART_LOGGER_PACKET], logger_sample_t sample)
{
    packet[0] = UART_LOGGER_SYNC_0;
    packet[1] = UART_LOGGER_SYNC_1;
    packet[2] = (uint8_t)sample.seq;
    packet[3] = (uint8_t)(sample.seq >> 8U);
    packet[4] = (uint8_t)sample.value;
    packet[5] = (uint8_t)((uint16_t)sample.value >> 8U);
    uint16_t crc = packet_crc(packet + 2U);
    packet[6] = (uint8_t)crc;
    packet[7] = (uint8_t)(crc >> 8U);
}

static void resynchronize(logger_stream_t *stream)
{
    for(uint8_t i = 1U; i + 1U < UART_LOGGER_PACKET; ++i)
    {
        if(stream->packet[i] == UART_LOGGER_SYNC_0 && stream->packet[i + 1U] == UART_LOGGER_SYNC_1)
        {
            stream->used = UART_LOGGER_PACKET - i;
            memmove(stream->packet, stream->packet + i, stream->used);
            return;
        }
    }
    stream->used = stream->packet[UART_LOGGER_PACKET - 1U] == UART_LOGGER_SYNC_0;
    if(stream->used)
        stream->packet[0] = UART_LOGGER_SYNC_0;
}

uint8_t logger_feed(logger_stream_t *stream, uint8_t byte, logger_sample_t *sample)
{
    if(stream->used == 0U && byte != UART_LOGGER_SYNC_0)
        return 0U;
    if(stream->used == 1U && byte != UART_LOGGER_SYNC_1)
    {
        stream->used = byte == UART_LOGGER_SYNC_0;
        return 0U;
    }
    stream->packet[stream->used++] = byte;
    if(stream->used != UART_LOGGER_PACKET)
        return 0U;
    if(packet_crc(stream->packet + 2U) != decode_u16(stream->packet + 6U))
    {
        stream->crc_errors++;
        resynchronize(stream);
        return 0U;
    }
    stream->used = 0U;
    const uint16_t seq = decode_u16(stream->packet + 2U);
    if(stream->accepted)
    {
        uint16_t advance = (uint16_t)(seq - stream->last_seq);
        if(advance == 0U || advance >= 32768U)
        {
            stream->stale++;
            return 0U;
        }
        stream->gaps += advance - 1U;
    }
    uint16_t raw = decode_u16(stream->packet + 4U);
    const int16_t value = (int16_t)((int32_t)raw - (raw >= 32768U ? 65536 : 0));
    stream->last_seq = seq;
    stream->last_value = value;
    stream->accepted++;
    for(unsigned i = 2U; i < 6U; ++i)
        stream->digest = (stream->digest ^ stream->packet[i]) * UINT32_C(16777619);
    stream->history[stream->next] = value;
    stream->next = (stream->next + 1U) % UART_LOGGER_HISTORY;
    if(stream->count < UART_LOGGER_HISTORY)
        stream->count++;
    *sample = (logger_sample_t){seq, value};
    return 1U;
}

size_t logger_csv_row(char *row, size_t size, logger_sample_t sample)
{
    int written = snprintf(row, size, "%u,%d\n", (unsigned)sample.seq, (int)sample.value);
    return written > 0 && (size_t)written < size ? (size_t)written : 0U;
}

static const logger_rect_t s_grid[] = {
    {20U, 35U, 200U, 1U, 0x528aU}, {20U, 185U, 200U, 1U, 0x528aU},
    {20U, 35U, 1U, 150U, 0x528aU}, {219U, 35U, 1U, 150U, 0x528aU},
    {21U, 110U, 198U, 1U, 0x3186U},
};

uint8_t logger_rect_count(const logger_stream_t *stream)
{
    return sizeof(s_grid) / sizeof(s_grid[0]) + stream->count;
}

logger_rect_t logger_rect(const logger_stream_t *stream, uint8_t index)
{
    const unsigned grid_count = sizeof(s_grid) / sizeof(s_grid[0]);
    if(index < grid_count)
        return s_grid[index];
    unsigned point = index - grid_count;
    unsigned history = (stream->next + UART_LOGGER_HISTORY - stream->count + point) % UART_LOGGER_HISTORY;
    int value = stream->history[history];
    if(value < -1000) value = -1000;
    if(value > 1000) value = 1000;
    unsigned x = 24U + (UART_LOGGER_HISTORY - stream->count + point) * 2U;
    unsigned y = 38U + (1000 - value) * 144U / 2000U;
    return (logger_rect_t){x, y, 2U, 2U, 0x07ffU};
}
