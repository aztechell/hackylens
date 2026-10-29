#ifndef HK_IMAGE_VIEWPORT_H
#define HK_IMAGE_VIEWPORT_H

#include <stdint.h>

typedef enum
{
    IMAGE_VIEWPORT_CONTAIN = 0, /* Entire source, with centered destination margins. */
    IMAGE_VIEWPORT_COVER,       /* Entire destination, with a centered source crop. */
} image_viewport_mode_t;

typedef struct
{
    uint16_t src_x, src_y, src_w, src_h;
    uint16_t dst_x, dst_y, dst_w, dst_h;
} image_viewport_t;

/* Geometry only: dimensions are independent of boards, displays and rotation.
 * Source coordinates refer to the image after any caller-owned rotation. */
uint8_t image_viewport_calculate(uint16_t src_w, uint16_t src_h,
                                  uint16_t dst_w, uint16_t dst_h,
                                  image_viewport_mode_t mode,
                                  image_viewport_t *viewport);

#endif
