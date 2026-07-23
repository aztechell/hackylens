#include "hk_ui.h"

#include "../core/hk_app.h"

#include "../config/display_config.h"
#include "../config/menu_layout.h"
#include "../drivers/hk_lcd.h"

static uint8_t s_topbar_sd_mounted;

void topbar_set_sd_mounted(uint8_t mounted)
{
    s_topbar_sd_mounted = mounted ? 1 : 0;
}

void topbar_draw_sd_status(void)
{
    uint16_t x = LCD_W - 23;
    uint16_t y = 7;

    if(!s_topbar_sd_mounted)
        return;

    lcd_fill_rect(x + 2, y, 10, 2, COLOR_TERM_GREEN);
    lcd_fill_rect(x + 12, y + 2, 2, 10, COLOR_TERM_GREEN);
    lcd_fill_rect(x, y + 3, 2, 11, COLOR_TERM_GREEN);
    lcd_fill_rect(x + 2, y + 12, 12, 2, COLOR_TERM_GREEN);
    lcd_fill_rect(x + 9, y, 2, 5, COLOR_TERM_GREEN);
    lcd_fill_rect(x + 11, y + 4, 3, 2, COLOR_TERM_GREEN);
    for(uint8_t i = 0; i < 4; i++)
        lcd_fill_rect(x + 3 + i * 3, y + 9, 2, 3, COLOR_TERM_GREEN);
}

void menu_draw_chrome(const char *title)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COLOR_BLACK);
    lcd_draw_rect(0, 0, LCD_W, LCD_H, MENU_LINE, COLOR_TERM_GREEN);
    lcd_fill_rect(0, MENU_BAR_H - MENU_LINE, LCD_W, MENU_LINE, COLOR_TERM_GREEN);
    lcd_draw_text_at(4, 3, "<o>", COLOR_TERM_GREEN, COLOR_BLACK);
    lcd_draw_text_centered(3, title, COLOR_TERM_GREEN, COLOR_BLACK);
    topbar_draw_sd_status();
}

void menu_draw_title(const char *title)
{
    lcd_fill_rect(52, 2, 190, MENU_BAR_H - 4 - MENU_LINE, COLOR_BLACK);
    lcd_draw_text_centered(3, title, COLOR_TERM_GREEN, COLOR_BLACK);
}

void menu_draw_item_at(uint8_t index, const hk_app_t *app, uint8_t selected)
{
    uint8_t col = (uint8_t)(index % MENU_GRID_COLS);
    uint8_t row = (uint8_t)(index / MENU_GRID_COLS);
    uint16_t x = MENU_ICON_X0 + col * (MENU_ICON + MENU_ICON_GAP_X);
    uint16_t y = MENU_ICON_Y0 + row * (MENU_ICON + MENU_ICON_GAP_Y);
    uint16_t fg = selected ? COLOR_BLACK : COLOR_TERM_GREEN;
    uint16_t bg = selected ? COLOR_TERM_GREEN : COLOR_BLACK;

    lcd_fill_rect(x, y, MENU_ICON, MENU_ICON, bg);
    if(selected)
        lcd_draw_rect(x + 4, y + 4, MENU_ICON - 8, MENU_ICON - 8, MENU_LINE, COLOR_BLACK);
    else
        lcd_draw_rect(x, y, MENU_ICON, MENU_ICON, MENU_LINE, COLOR_TERM_GREEN);

    if(app->draw_icon)
        app->draw_icon(x, y, fg, bg);
}
