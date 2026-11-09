#ifndef HACKYLENS_APP_PANEL_H
#define HACKYLENS_APP_PANEL_H

#include <hackylens/app.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    HK_PANEL_BLACK = 0x0000U,
    HK_PANEL_GREEN = 0x07e0U,
    HK_PANEL_TRACK = 0x0320U,
};

typedef enum
{
    HK_PANEL_LEFT,
    HK_PANEL_CENTER,
    HK_PANEL_RIGHT,
} hk_panel_align_t;

/* A layout helper for the borrowed surface of the current render callback. */
typedef struct
{
    hk_app_surface_t *surface;
    uint16_t width;
    uint16_t height;
    hk_result_t result;
} hk_app_panel_t;

hk_app_panel_t hk_app_panel_begin(hk_app_surface_t *surface);
void hk_app_panel_chrome(hk_app_panel_t *panel, const char *title);
void hk_app_panel_fill(hk_app_panel_t *panel, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height, uint16_t color);
void hk_app_panel_label(hk_app_panel_t *panel, uint16_t x, uint16_t y, uint16_t width,
                        const char *text, uint16_t color, hk_panel_align_t align);
void hk_app_panel_status(hk_app_panel_t *panel, uint16_t y, const char *status);
void hk_app_panel_volume(hk_app_panel_t *panel, uint16_t y, uint8_t volume);

#ifdef __cplusplus
}
#endif

#endif
