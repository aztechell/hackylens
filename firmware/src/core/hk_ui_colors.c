#include "hk_ui_colors.h"

#define DEFAULT_ACCENT 0x07e0U

static uint16_t s_accent = DEFAULT_ACCENT;

uint16_t hk_ui_accent(void)
{
    return s_accent;
}

uint16_t hk_ui_accent_dim(void)
{
    return (uint16_t)((s_accent & 0xf7deU) >> 1);
}

void hk_ui_preview_color(uint8_t color)
{
    static const uint16_t colors[HK_UI_COLOR_COUNT] = {
        [HK_UI_COLOR_GREEN] = DEFAULT_ACCENT,
        [HK_UI_COLOR_CYAN] = 0x07ffU,
        [HK_UI_COLOR_AMBER] = 0xfd60U,
        [HK_UI_COLOR_PINK] = 0xfb1aU,
        [HK_UI_COLOR_BLUE] = 0x5dffU,
    };
    s_accent = colors[hk_ui_color_normalize(color)];
}
