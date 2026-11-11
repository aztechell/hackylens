#ifndef QR_DECODER_ENGINE_H
#define QR_DECODER_ENGINE_H

#include <stdint.h>

#include "qr_config.h"

typedef struct
{
    int code_count;
    uint8_t found;
    uint16_t x0;
    uint16_t y0;
    uint16_t x1;
    uint16_t y1;
    uint16_t payload_len;
    uint8_t payload[QR_PAYLOAD_MAX - 1U];
} qr_engine_scan_t;

/* Only the executor owns quirc. The returned image can be shared through its
 * uncached alias once the prepare ticket is complete. */
uint8_t *qr_decoder_engine_prepare(uint16_t width, uint16_t height);
void qr_decoder_engine_scan(uint8_t *image, qr_engine_scan_t *scan);
void qr_decoder_engine_close(void);

#endif
