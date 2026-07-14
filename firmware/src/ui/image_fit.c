#include "hk_ui.h"

#include "../config/display_config.h"

void image_fit_viewport(uint16_t src_w, uint16_t src_h, uint16_t *dst_x, uint16_t *dst_y, uint16_t *dst_w, uint16_t *dst_h)
{
    uint32_t fit_w = LCD_W;
    uint32_t fit_h = LCD_H;

    if(src_w == 0 || src_h == 0)
    {
        *dst_x = 0;
        *dst_y = 0;
        *dst_w = LCD_W;
        *dst_h = LCD_H;
        return;
    }

    if((uint32_t)src_w * LCD_H > (uint32_t)src_h * LCD_W)
        fit_h = ((uint32_t)src_h * LCD_W + src_w / 2U) / src_w;
    else
        fit_w = ((uint32_t)src_w * LCD_H + src_h / 2U) / src_h;

    if(fit_w == 0)
        fit_w = 1;
    if(fit_h == 0)
        fit_h = 1;
    if(fit_w > LCD_W)
        fit_w = LCD_W;
    if(fit_h > LCD_H)
        fit_h = LCD_H;

    *dst_w = (uint16_t)fit_w;
    *dst_h = (uint16_t)fit_h;
    *dst_x = (uint16_t)((LCD_W - *dst_w) / 2U);
    *dst_y = (uint16_t)((LCD_H - *dst_h) / 2U);
}
