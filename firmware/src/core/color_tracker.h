#ifndef HK_COLOR_TRACKER_H
#define HK_COLOR_TRACKER_H

#include <stdint.h>

/* A landscape sensor frame up to VGA. Rotate clockwise for portrait output. */
#define HK_COLOR_MAX_WIDTH 640U
#define HK_COLOR_MAX_HEIGHT 480U
#define HK_COLOR_MAX_RUNS ((HK_COLOR_MAX_WIDTH + 1U) / 2U)
#define HK_COLOR_MAX_COMPONENTS (2U * HK_COLOR_MAX_RUNS)

typedef struct
{
    const volatile uint16_t *pixels;
    uint16_t width;
    uint16_t height;
    uint8_t rotate;
} hk_color_frame_t;

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} hk_color_rect_t;

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

typedef struct
{
    uint16_t x0, x1, label;
} hk_color_run_t;

typedef struct
{
    hk_color_blob_t blob;
    uint16_t parent;
} hk_color_component_t;

/* One synchronous scan per workspace. The tracker keeps only two rows of
 * runs, with no allocations or per-frame mask. */
typedef struct
{
    hk_color_run_t rows[2][HK_COLOR_MAX_RUNS];
    hk_color_component_t components[HK_COLOR_MAX_COMPONENTS];
    uint16_t remap[HK_COLOR_MAX_COMPONENTS];
} hk_color_tracker_t;

/* ROI and result use logical frame coordinates after rotation. Bounds are
 * x, y, width, height. Channel thresholds are inclusive RGB565 native bins
 * (0..31, 0..63, 0..31). Returns zero on invalid input. On a successful scan
 * with no match, blob->pixels is zero. Ties favor the topmost, then leftmost. */
uint8_t hk_color_find_largest(hk_color_tracker_t *tracker,
                              const hk_color_frame_t *frame,
                              const hk_color_rect_t *roi,
                              const hk_color_threshold_t *threshold,
                              uint32_t min_pixels, hk_color_blob_t *blob);

#endif
