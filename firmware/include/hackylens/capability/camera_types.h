#ifndef HACKYLENS_CAMERA_TYPES_H
#define HACKYLENS_CAMERA_TYPES_H

#include <stdint.h>

/* Rectangles use logical frame coordinates after board mounting rotation. */
typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} hk_color_rect_t;

/* Inclusive RGB565 bins: 0..31 for red/blue, 0..63 for green. */
typedef struct
{
    uint8_t red_min, red_max;
    uint8_t green_min, green_max;
    uint8_t blue_min, blue_max;
} hk_color_threshold_t;

typedef struct
{
    hk_color_rect_t bounds;
    uint32_t pixels;
} hk_color_blob_t;

#endif
