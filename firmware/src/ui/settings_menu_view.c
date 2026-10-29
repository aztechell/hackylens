#include "settings_menu_view.h"

#include <string.h>

#include "../config/display_config.h"
#include "../config/settings_menu_layout.h"
#include "../ui/display_binding.h"
#include "hk_ui.h"

static void settings_menu_view_clear_slot(uint8_t slot)
{
    uint16_t y;

    if(slot >= SETTINGS_MENU_VISIBLE_ROWS)
        return;
    y = SETTINGS_MENU_ROW_Y0 + slot * SETTINGS_MENU_ROW_H;
    hk_ui_display_fill_rect(6, y, HK_DISPLAY_REQUIRED_WIDTH - 12, SETTINGS_MENU_ROW_H - 2, COLOR_BLACK);
}

void settings_menu_view_open(const char *title)
{
    menu_draw_chrome(title ? title : "SETTINGS");
}

void settings_menu_view_clear_rows(void)
{
    for(uint8_t slot = 0U; slot < SETTINGS_MENU_VISIBLE_ROWS; slot++)
        settings_menu_view_clear_slot(slot);
}

void settings_menu_view_draw_row(uint8_t slot,
                                 const char *title,
                                 const char *value,
                                 uint8_t selected,
                                 uint8_t editing)
{
    uint16_t y;
    uint16_t fg;
    uint16_t bg;
    uint16_t value_x;
    size_t value_length;

    if(slot >= SETTINGS_MENU_VISIBLE_ROWS)
        return;
    title = title ? title : "";
    value = value ? value : "";
    selected = selected ? 1U : 0U;
    fg = selected ? COLOR_BLACK : COLOR_TERM_GREEN;
    bg = selected ? COLOR_TERM_GREEN : COLOR_BLACK;
    y = SETTINGS_MENU_ROW_Y0 + slot * SETTINGS_MENU_ROW_H;
    value_length = strlen(value);
    {
        uint32_t value_width = (uint32_t)value_length * HACKYLENS_FONT_W;
        if(value_width > HK_DISPLAY_REQUIRED_WIDTH - 20U)
            value_width = HK_DISPLAY_REQUIRED_WIDTH - 20U;
        value_x = HK_DISPLAY_REQUIRED_WIDTH - 10U - value_width;
        if(editing && value_x < 10U + HACKYLENS_FONT_W)
            value_x = 10U + HACKYLENS_FONT_W;
    }

    hk_ui_display_fill_rect(6, y, HK_DISPLAY_REQUIRED_WIDTH - 12, SETTINGS_MENU_ROW_H - 2, bg);
    hk_ui_display_draw_text_clipped_at(10, y + 3,
        value_x > 12U + (editing ? HACKYLENS_FONT_W : 0U) ?
            value_x - 12U - (editing ? HACKYLENS_FONT_W : 0U) : 0U,
        title, fg, bg);
    if(editing)
        hk_ui_display_draw_text_at(value_x - HACKYLENS_FONT_W, y + 3, "*", fg, bg);
    hk_ui_display_draw_text_at(value_x, y + 3, value, fg, bg);
}
