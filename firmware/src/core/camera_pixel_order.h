#ifndef HK_CAMERA_PIXEL_ORDER_H
#define HK_CAMERA_PIXEL_ORDER_H

#include <stdint.h>

/* K210 DVP writes each RGB565 pair with its right pixel in the low
 * halfword. All supported capture widths contain complete pairs. */
static inline uint32_t camera_dvp_pair_xor(uint32_t width)
{
    return (width & 1U) == 0U;
}

#endif
