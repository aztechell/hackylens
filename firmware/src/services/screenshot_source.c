#include "screenshot_source.h"

#include <stddef.h>

#include "../drivers/hk_lcd.h"

static uint16_t screenshot_lcd_shadow_pixel(void *context, uint16_t x, uint16_t y)
{
    (void)context;
    return lcd_shadow_pixel(x, y);
}

static const screenshot_pixel_source_t g_lcd_shadow_source = {
    .pixel_at = screenshot_lcd_shadow_pixel,
    .context = NULL,
};

const screenshot_pixel_source_t *screenshot_source_lcd_shadow(void)
{
    return &g_lcd_shadow_source;
}
