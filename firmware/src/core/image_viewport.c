#include "image_viewport.h"

uint8_t image_viewport_calculate(uint16_t src_w, uint16_t src_h,
                                  uint16_t dst_w, uint16_t dst_h,
                                  image_viewport_mode_t mode,
                                  image_viewport_t *viewport)
{
    if(!viewport)
        return 0;
    *viewport = (image_viewport_t){0};
    if(!src_w || !src_h || !dst_w || !dst_h ||
       (mode != IMAGE_VIEWPORT_CONTAIN && mode != IMAGE_VIEWPORT_COVER))
        return 0;

    viewport->src_w = src_w;
    viewport->src_h = src_h;
    viewport->dst_w = dst_w;
    viewport->dst_h = dst_h;
    uint8_t source_is_wider = (uint32_t)src_w * dst_h > (uint32_t)src_h * dst_w;

    if(mode == IMAGE_VIEWPORT_COVER)
    {
        if(source_is_wider)
        {
            uint32_t crop_w = (uint32_t)src_h * dst_w / dst_h;
            viewport->src_w = (uint16_t)(crop_w ? crop_w : 1U);
            viewport->src_x = (src_w - viewport->src_w) / 2U;
        }
        else
        {
            uint32_t crop_h = (uint32_t)src_w * dst_h / dst_w;
            viewport->src_h = (uint16_t)(crop_h ? crop_h : 1U);
            viewport->src_y = (src_h - viewport->src_h) / 2U;
        }
    }
    else
    {
        if(source_is_wider)
        {
            uint32_t fit_h = ((uint32_t)src_h * dst_w + src_w / 2U) / src_w;
            viewport->dst_h = (uint16_t)(fit_h ? fit_h : 1U);
        }
        else
        {
            uint32_t fit_w = ((uint32_t)src_w * dst_h + src_h / 2U) / src_h;
            viewport->dst_w = (uint16_t)(fit_w ? fit_w : 1U);
        }
        viewport->dst_x = (dst_w - viewport->dst_w) / 2U;
        viewport->dst_y = (dst_h - viewport->dst_h) / 2U;
    }
    return 1;
}
