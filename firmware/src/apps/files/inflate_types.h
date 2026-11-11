#ifndef HK_INFLATE_TYPES_H
#define HK_INFLATE_TYPES_H

#include <stdint.h>

typedef struct
{
    const uint8_t *data;
    uint32_t size;
    uint32_t pos;
    uint32_t bitbuf;
    uint8_t bitcount;
    uint16_t *literals;
    uint16_t *distances;
} inflate_stream_t;

#endif
