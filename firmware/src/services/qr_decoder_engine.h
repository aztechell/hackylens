#ifndef QR_DECODER_ENGINE_H
#define QR_DECODER_ENGINE_H

#include <stdint.h>

#include <hackylens/capability/vision.h>

struct quirc;

typedef struct
{
    struct quirc *decoder;
    uint16_t width;
    uint16_t height;
} qr_decoder_engine_t;

typedef struct
{
    int code_count;
    uint8_t found;
    uint16_t x0;
    uint16_t y0;
    uint16_t x1;
    uint16_t y1;
    uint16_t payload_len;
    uint8_t payload[VISION_RESULT_MAX_QR_BYTES];
} qr_engine_scan_t;

/* Run on core 1. Core 0 can fill the returned image through its uncached alias
 * while this decoder is idle. */
uint8_t *qr_decoder_engine_prepare(qr_decoder_engine_t *engine,
                                   uint16_t width, uint16_t height);
void qr_decoder_engine_scan(qr_decoder_engine_t *engine,
                            uint8_t *image, qr_engine_scan_t *scan);
void qr_decoder_engine_close(qr_decoder_engine_t *engine);

#endif
