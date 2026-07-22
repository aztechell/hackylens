#include "settings_menu_view.h"

#include <string.h>

#include "../config/display_config.h"
#include "../config/settings_menu_layout.h"
#include "../drivers/hk_lcd.h"
#include "hk_ui.h"

static void settings_menu_view_clear_slot(uint8_t slot)
{
    uint16_t y;

    if(slot >= SETTINGS_MENU_VISIBLE_ROWS)
        return;
    y = SETTINGS_MENU_ROW_Y0 + slot * SETTINGS_MENU_ROW_H;
    lcd_fill_rect(6, y, LCD_W - 12, SETTINGS_MENU_ROW_H - 2, COLOR_BLACK);
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
    value_x = LCD_W - 10U - (uint16_t)(value_length * HACKYLENS_FONT_W);

    lcd_fill_rect(6, y, LCD_W - 12, SETTINGS_MENU_ROW_H - 2, bg);
    lcd_draw_text_at(10, y + 3, title, fg, bg);
    if(editing)
        lcd_draw_text_at(value_x - HACKYLENS_FONT_W, y + 3, "*", fg, bg);
    lcd_draw_text_at(value_x, y + 3, value, fg, bg);
}
