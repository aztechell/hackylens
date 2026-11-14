#ifndef HK_UI_COLORS_H
#define HK_UI_COLORS_H

#include <stdint.h>

typedef enum
{
    HK_UI_COLOR_GREEN = 0,
    HK_UI_COLOR_CYAN,
    HK_UI_COLOR_AMBER,
    HK_UI_COLOR_PINK,
    HK_UI_COLOR_BLUE,
    HK_UI_COLOR_WHITE,
    HK_UI_COLOR_COUNT,
} hk_ui_color_t;

static inline hk_ui_color_t hk_ui_color_normalize(uint8_t color)
{
    return color < HK_UI_COLOR_COUNT ? (hk_ui_color_t)color : HK_UI_COLOR_GREEN;
}

uint16_t hk_ui_accent(void);
uint16_t hk_ui_accent_dim(void);
void hk_ui_preview_color(uint8_t color);

#endif
