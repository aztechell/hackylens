#include <hackylens/app/panel.h>
#include "../core/hk_ui_colors.h"

#include <stdio.h>
#include <string.h>

#include "hk_ui.h"
#include "../config/menu_layout.h"

hk_app_panel_t hk_app_panel_begin(hk_app_surface_t *surface)
{
    hk_display_info_t info;
    hk_app_panel_t panel = {.surface = surface};
    panel.result = hk_app_surface_get_info(surface, &info);
    if(panel.result == HK_OK)
    {
        panel.width = info.width;
        panel.height = info.height;
        if(info.width < 240U || info.height < 240U)
            panel.result = HK_ERR_FEATURE_UNAVAILABLE;
    }
    return panel;
}

void hk_app_panel_fill(hk_app_panel_t *panel, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height, uint16_t color)
{
    const hk_display_rect_t bounds = {x, y, width, height};
    if(panel->result == HK_OK && width && height)
        panel->result = hk_app_surface_fill_rect(panel->surface, &bounds, color);
}

static uint8_t utf8_lead(char byte)
{
    return ((uint8_t)byte & 0xc0U) != 0x80U;
}

void hk_app_panel_label(hk_app_panel_t *panel, uint16_t x, uint16_t y, uint16_t width,
                        const char *text, uint16_t color, hk_panel_align_t align)
{
    if(panel->result != HK_OK || !text || !*text || width < HACKYLENS_FONT_W)
        return;
    uint16_t columns = width / HACKYLENS_FONT_W;
    uint16_t count = 0U;
    for(const char *cursor = text; *cursor && count <= columns; cursor++)
        count += utf8_lead(*cursor);

    char fitted[128];
    if(count > columns)
    {
        const char *end = text;
        uint16_t retained = columns > 3U ? columns - 3U : 0U;
        count = 0U;
        while(*end && (count < retained || !utf8_lead(*end)) &&
              (size_t)(end - text) < sizeof(fitted) - 4U)
            count += utf8_lead(*end++);
        size_t bytes = (size_t)(end - text);
        memcpy(fitted, text, bytes);
        uint16_t dots = columns < 3U ? columns : 3U;
        memset(fitted + bytes, '.', dots);
        fitted[bytes + dots] = '\0';
        text = fitted;
        count += dots;
    }

    uint16_t text_width = count * HACKYLENS_FONT_W;
    if(align == HK_PANEL_CENTER)
        x += (width - text_width) / 2U;
    else if(align == HK_PANEL_RIGHT)
        x += width - text_width;
    const hk_display_rect_t bounds = {x, y, text_width, HACKYLENS_FONT_H};
    panel->result = hk_app_surface_text(panel->surface, &bounds, text,
                                        (uint32_t)strlen(text), color);
}

void hk_app_panel_chrome(hk_app_panel_t *panel, const char *title)
{
    if(panel->result != HK_OK)
        return;
    panel->result = hk_app_surface_clear(panel->surface, HK_PANEL_BLACK);
    const hk_display_rect_t outer = {0, 0, panel->width, panel->height};
    const hk_display_rect_t inner = {1, 1, panel->width - 2U, panel->height - 2U};
    if(panel->result == HK_OK)
        panel->result = hk_app_surface_stroke_rect(panel->surface, &outer, hk_ui_accent());
    if(panel->result == HK_OK)
        panel->result = hk_app_surface_stroke_rect(panel->surface, &inner, hk_ui_accent());
    hk_app_panel_fill(panel, 0, MENU_BAR_H - MENU_LINE, panel->width, MENU_LINE, hk_ui_accent());
    hk_app_panel_label(panel, 24U, 3U, panel->width - 48U, title, hk_ui_accent(), HK_PANEL_CENTER);
    if(!topbar_sd_mounted())
        return;
    uint16_t x = panel->width - 23U;
    hk_app_panel_fill(panel, x + 2U, 7U, 10U, 2U, hk_ui_accent());
    hk_app_panel_fill(panel, x + 12U, 9U, 2U, 10U, hk_ui_accent());
    hk_app_panel_fill(panel, x, 10U, 2U, 11U, hk_ui_accent());
    hk_app_panel_fill(panel, x + 2U, 19U, 12U, 2U, hk_ui_accent());
    hk_app_panel_fill(panel, x + 9U, 7U, 2U, 5U, hk_ui_accent());
    hk_app_panel_fill(panel, x + 11U, 11U, 3U, 2U, hk_ui_accent());
    for(uint8_t i = 0U; i < 4U; i++)
        hk_app_panel_fill(panel, x + 3U + i * 3U, 16U, 2U, 3U, hk_ui_accent());
}

void hk_app_panel_status(hk_app_panel_t *panel, uint16_t y, const char *status)
{
    if(panel->result != HK_OK || !status || !*status)
        return;
    uint16_t width = panel->width - 24U;
    size_t columns = width / HACKYLENS_FONT_W;
    if(strlen(status) <= columns)
    {
        hk_app_panel_label(panel, 12U, y, width, status, hk_ui_accent(), HK_PANEL_CENTER);
        return;
    }
    /* Recorder and player status messages are ASCII. */
    size_t split = columns;
    while(split && status[split] != ' ')
        split--;
    if(!split)
        split = columns;
    char first[128];
    if(split >= sizeof(first))
        split = sizeof(first) - 1U;
    memcpy(first, status, split);
    first[split] = '\0';
    hk_app_panel_label(panel, 12U, y, width, first, hk_ui_accent(), HK_PANEL_CENTER);
    status += split;
    while(*status == ' ')
        status++;
    hk_app_panel_label(panel, 12U, y + HACKYLENS_FONT_H, width, status,
                       hk_ui_accent(), HK_PANEL_CENTER);
}

void hk_app_panel_volume(hk_app_panel_t *panel, uint16_t y, uint8_t volume)
{
    if(panel->result != HK_OK)
        return;
    char level[8];
    uint16_t width = panel->width - 24U;
    (void)snprintf(level, sizeof(level), "%u%%", (unsigned)volume);
    hk_app_panel_label(panel, 12U, y, width, "VOLUME", hk_ui_accent(), HK_PANEL_LEFT);
    hk_app_panel_label(panel, 12U, y, width, level, hk_ui_accent(), HK_PANEL_RIGHT);
    for(uint8_t i = 0U; i < 10U; i++)
    {
        uint16_t left = 12U + width * i / 10U;
        uint16_t right = 12U + width * (i + 1U) / 10U;
        hk_app_panel_fill(panel, left, y + 30U, right - left - 2U, 8U,
                          volume > i * 10U ? hk_ui_accent() : hk_ui_accent_dim());
    }
}
