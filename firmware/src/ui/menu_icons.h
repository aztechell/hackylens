#ifndef HK_UI_MENU_ICONS_H
#define HK_UI_MENU_ICONS_H

#include "display_binding.h"
#include "hackylens_menu_icons_1bpp.h"

/* One bounded surface update per glyph, including its background for selection. */
static inline void hk_menu_icon_draw(
    uint16_t x, uint16_t y, uint16_t foreground, uint16_t background,
    const uint8_t *mask)
{
    hk_ui_display_surface_t surface;
    uint32_t left = (uint32_t)x + 6U;
    uint32_t top = (uint32_t)y + 6U;
    hk_ui_display_rect_t region;

    if(!hk_ui_display_frame_acquire(&surface))
        return;
    if(left + HK_MENU_ICON_SIZE > surface.width ||
       top + HK_MENU_ICON_SIZE > surface.height)
    {
        hk_ui_display_frame_cancel(surface.lease_id);
        return;
    }
    for(unsigned row = 0U; row < HK_MENU_ICON_SIZE; row++)
    {
        uint8_t *out = surface.rgb565_be +
            (top + row) * surface.stride_bytes + left * 2U;
        for(unsigned column = 0U; column < HK_MENU_ICON_SIZE; column++)
        {
            uint8_t bit = mask[row * HK_MENU_ICON_ROW_BYTES + column / 8U] &
                          (uint8_t)(0x80U >> (column & 7U));
            uint16_t color = bit ? foreground : background;
            out[column * 2U] = (uint8_t)(color >> 8);
            out[column * 2U + 1U] = (uint8_t)color;
        }
    }
    region = (hk_ui_display_rect_t){
        (uint16_t)left, (uint16_t)top, HK_MENU_ICON_SIZE, HK_MENU_ICON_SIZE,
    };
    if(!hk_ui_display_frame_present_regions(surface.lease_id, &region, 1U))
        hk_ui_display_frame_cancel(surface.lease_id);
}

#endif
