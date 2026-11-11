#ifndef QR_SERVICE_H
#define QR_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "qr_config.h"


typedef enum
{
    QR_DECODE_IDLE = 0,
    QR_DECODE_NO_FRAME,
    QR_DECODE_DECODER_FAIL,
    QR_DECODE_NOT_FOUND,
    QR_DECODE_FOUND,
} qr_decode_result_t;

void qr_service_enter(void);
void qr_service_exit(void);
void qr_service_submit_frame(const volatile uint16_t *pixels, uint16_t width,
                             uint16_t height, uint32_t sequence, void *context);
qr_decode_result_t qr_service_poll_decode(void);
void qr_service_discard_scan(void);
qr_decode_result_t qr_service_decode_force(void);
void qr_service_format_info(char *line, size_t line_size, const char *screen);

uint8_t qr_service_decode_rate(void);
void qr_service_set_decode_rate(uint8_t rate);
uint8_t qr_service_cycle_decode_rate(void);

#endif
