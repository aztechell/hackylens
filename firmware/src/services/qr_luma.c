#include "qr_luma.h"

#include "../core/camera_pixel_order.h"

static uint8_t qr_rgb565_luma(uint16_t color)
{
    uint8_t r = (uint8_t)((color >> 8) & 0xF8U);
    uint8_t g = (uint8_t)((color >> 3) & 0xFCU);
    uint8_t b = (uint8_t)((color << 3) & 0xF8U);
    return (uint8_t)(((uint16_t)r * 30U + (uint16_t)g * 59U + (uint16_t)b * 11U) / 100U);
}

void qr_luma_fill_local_qvga(const volatile uint16_t *src, uint8_t *image, uint8_t mode)
{
    enum { WIDTH = 320, HEIGHT = 240, RADIUS = 8, BIAS = 3 };
    uint16_t columns[WIDTH] = {0};
    unsigned top = 0U;
    unsigned bottom = RADIUS;
    const unsigned pair_xor = (mode & QR_LUMA_DVP_PAIRS) != 0U;

    for(unsigned y = 0U; y <= bottom; y++)
        for(unsigned x = 0U; x < WIDTH; x++)
            columns[x] += qr_rgb565_luma(src[y * WIDTH + (x ^ pair_xor)]);

    for(unsigned y = 0U; y < HEIGHT; y++)
    {
        const unsigned rows = bottom - top + 1U;
        unsigned left = 0U;
        unsigned right = RADIUS;
        uint32_t sum = 0U;
        for(unsigned x = 0U; x <= right; x++)
            sum += columns[x];

        for(unsigned x = 0U; x < WIDTH; x++)
        {
            unsigned luma = qr_rgb565_luma(src[y * WIDTH + (x ^ pair_xor)]);
            unsigned samples = rows * (right - left + 1U);
            image[y * WIDTH + x] = (luma + BIAS) * samples < sum ? 0U : 255U;
            if(x >= RADIUS)
                sum -= columns[left++];
            if(right + 1U < WIDTH)
                sum += columns[++right];
        }

        if(y >= RADIUS)
        {
            const volatile uint16_t *old_row = src + top++ * WIDTH;
            for(unsigned x = 0U; x < WIDTH; x++)
                columns[x] -= qr_rgb565_luma(old_row[x ^ pair_xor]);
        }
        if(bottom + 1U < HEIGHT)
        {
            const volatile uint16_t *new_row = src + ++bottom * WIDTH;
            for(unsigned x = 0U; x < WIDTH; x++)
                columns[x] += qr_rgb565_luma(new_row[x ^ pair_xor]);
        }
    }
}

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
                        qr_luma_stats_t *stats)
{
    uint32_t sum = 0;
    uint8_t min = stats ? stats->min : 0;
    uint8_t max = stats ? stats->max : 0;
    uint16_t range = max > min ? (uint16_t)(max - min) : 0;
    const uint32_t pair_xor = (mode & QR_LUMA_DVP_PAIRS) ? camera_dvp_pair_xor(src_stride) : 0U;

    if(update_stats)
    {
        min = 255;
        max = 0;
    }

    for(uint16_t y = 0; y < height; y++)
    {
        uint16_t src_y = (uint16_t)(src_y0 + (((uint32_t)y * src_h) / height));
        for(uint16_t x = 0; x < width; x++)
        {
            uint16_t src_x = (uint16_t)(src_x0 + (((uint32_t)x * src_w) / width));
            uint8_t luma = qr_rgb565_luma(src[(uint32_t)src_y * src_stride + (src_x ^ pair_xor)]);

            if(update_stats)
            {
                if(luma < min)
                    min = luma;
                if(luma > max)
                    max = luma;
                sum += luma;
            }
            else if((mode & QR_PASS_STRETCH) && range > 8U)
            {
                int16_t shifted = (int16_t)luma - (int16_t)min;
                if(shifted < 0)
                    shifted = 0;
                if(shifted > range)
                    shifted = (int16_t)range;
                luma = (uint8_t)((uint16_t)shifted * 255U / range);
            }

            if(mode & QR_PASS_INVERT)
                luma = (uint8_t)(255U - luma);
            image[(uint32_t)y * width + x] = luma;
        }
    }

    if(update_stats && stats)
    {
        stats->min = min;
        stats->max = max;
        stats->avg = (uint8_t)(sum / ((uint32_t)width * height));
    }
}
