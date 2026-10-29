#include "hk_ui.h"

#include "../config/display_config.h"
#include "../core/image_viewport.h"

void image_fit_viewport(uint16_t src_w, uint16_t src_h, uint16_t *dst_x, uint16_t *dst_y, uint16_t *dst_w, uint16_t *dst_h)
{
    image_viewport_t viewport;
    if(!image_viewport_calculate(src_w, src_h,
                                HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT,
                                IMAGE_VIEWPORT_CONTAIN, &viewport))
    {
        *dst_x = 0;
        *dst_y = 0;
        *dst_w = HK_DISPLAY_REQUIRED_WIDTH;
        *dst_h = HK_DISPLAY_REQUIRED_HEIGHT;
        return;
    }

    *dst_x = viewport.dst_x;
    *dst_y = viewport.dst_y;
    *dst_w = viewport.dst_w;
    *dst_h = viewport.dst_h;
}
