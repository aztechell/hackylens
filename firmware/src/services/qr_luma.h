#ifndef QR_LUMA_H
#define QR_LUMA_H

#include <stdint.h>

#define QR_PASS_INVERT 0x01U
#define QR_PASS_STRETCH 0x02U
#define QR_PASS_LOCAL 0x04U
#define QR_LUMA_DVP_PAIRS 0x08U

typedef struct
{
    uint8_t min;
    uint8_t avg;
    uint8_t max;
} qr_luma_stats_t;

void qr_luma_fill_image(const volatile uint16_t *src,
                        uint8_t *image,
                        uint16_t width,
                        uint16_t height,
                        uint16_t src_stride,
                        uint16_t src_x0,
                        uint16_t src_y0,
                        uint16_t src_w,
                        uint16_t src_h,
                        uint8_t mode,
                        uint8_t update_stats,
                        qr_luma_stats_t *stats);

/* Fill a 320x240 image using the mean luminance near each source pixel. */
void qr_luma_fill_local_qvga(const volatile uint16_t *src, uint8_t *image, uint8_t mode);

#endif
