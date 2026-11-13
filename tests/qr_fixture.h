#ifndef HK_TEST_QR_FIXTURE_H
#define HK_TEST_QR_FIXTURE_H

#include <stdint.h>

/* Version 1-L, mask 0, byte payload "HL". */
static const uint32_t qr_fixture_rows[] = {
    0x1fd47fU, 0x105041U, 0x17455dU, 0x17505dU, 0x175a5dU, 0x104e41U, 0x1fd57fU,
    0x000500U, 0x0475f7U, 0x0caf11U, 0x1cec51U, 0x0bbcbcU, 0x04e85dU, 0x0c4500U,
    0x19177fU, 0x1c4741U, 0x15535dU, 0x0aa85dU, 0x16eb5dU, 0x0bb941U, 0x1eef7fU,
};

/* Store a logical RGB565 test image in K210 DVP word order. */
static inline void pack_qr_fixture_dvp(uint16_t *pixels, uint32_t count)
{
    for(uint32_t i = 0U; i + 1U < count; i += 2U)
    {
        uint16_t left = pixels[i];
        pixels[i] = pixels[i + 1U];
        pixels[i + 1U] = left;
    }
}

static void draw_qr_fixture(uint16_t *pixels, uint16_t width, uint16_t height, uint8_t invert)
{
    for(uint32_t i = 0U; i < (uint32_t)width * height; i++)
        pixels[i] = invert ? 0U : UINT16_MAX;
    for(unsigned y = 0U; y < 84U; y++)
        for(unsigned x = 0U; x < 84U; x++)
            if(qr_fixture_rows[y / 4U] & (1U << (x / 4U)))
                pixels[(y + 48U) * width + x + 88U] = invert ? UINT16_MAX : 0U;
}

#endif
