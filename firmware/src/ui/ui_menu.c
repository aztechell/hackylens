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

void menu_draw_camera_icon(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_draw_rect(x + 10, y + 18, 40, 28, 2, color);
    lcd_fill_rect(x + 18, y + 12, 18, 6, color);
    lcd_draw_rect(x + 23, y + 24, 14, 14, 2, color);
    lcd_fill_rect(x + 28, y + 29, 4, 4, color);
    lcd_fill_rect(x + 43, y + 22, 4, 4, color);
}

void menu_draw_qr_camera_icon(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_draw_rect(x + 8, y + 14, 44, 32, 2, color);
    lcd_fill_rect(x + 18, y + 8, 18, 6, color);
    lcd_draw_rect(x + 14, y + 20, 10, 10, 2, color);
    lcd_draw_rect(x + 36, y + 20, 10, 10, 2, color);
    lcd_draw_rect(x + 14, y + 34, 10, 10, 2, color);
    lcd_fill_rect(x + 30, y + 34, 4, 4, color);
    lcd_fill_rect(x + 38, y + 36, 8, 2, color);
    lcd_fill_rect(x + 30, y + 42, 12, 2, color);
}

void menu_draw_files_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    lcd_fill_rect(x + 20, y + 8, 20, 2, color);
    lcd_fill_rect(x + 18, y + 12, 2, 38, color);
    lcd_fill_rect(x + 42, y + 14, 2, 36, color);
    lcd_fill_rect(x + 20, y + 50, 22, 2, color);
    lcd_fill_rect(x + 38, y + 8, 2, 8, color);
    lcd_fill_rect(x + 40, y + 14, 4, 2, color);
    lcd_fill_rect(x + 23, y + 15, 4, 8, color);
    lcd_fill_rect(x + 29, y + 15, 4, 8, color);
    lcd_fill_rect(x + 35, y + 15, 4, 8, color);
    lcd_fill_rect(x + 24, y + 39, 14, 2, color);
    lcd_fill_rect(x + 24, y + 44, 10, 2, color);
}

void menu_draw_buttons_icon(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_draw_rect(x + 13, y + 12, 14, 14, 2, color);
    lcd_draw_rect(x + 33, y + 12, 14, 14, 2, color);
    lcd_draw_rect(x + 13, y + 34, 14, 14, 2, color);
    lcd_draw_rect(x + 33, y + 34, 14, 14, 2, color);
}

void menu_draw_settings_icon(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_fill_rect(x + 27, y + 7, 6, 9, color);
    lcd_fill_rect(x + 27, y + 44, 6, 9, color);
    lcd_fill_rect(x + 7, y + 27, 9, 6, color);
    lcd_fill_rect(x + 44, y + 27, 9, 6, color);
    lcd_fill_rect(x + 14, y + 14, 7, 7, color);
    lcd_fill_rect(x + 39, y + 14, 7, 7, color);
    lcd_fill_rect(x + 14, y + 39, 7, 7, color);
    lcd_fill_rect(x + 39, y + 39, 7, 7, color);
    lcd_draw_rect(x + 18, y + 18, 24, 24, 3, color);
    lcd_draw_rect(x + 24, y + 24, 12, 12, 2, color);
    lcd_fill_rect(x + 28, y + 28, 4, 4, color);
}

void menu_draw_sleep_icon(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_draw_rect(x + 20, y + 10, 20, 30, 2, color);
    lcd_fill_rect(x + 28, y + 6, 4, 14, color);
    lcd_fill_rect(x + 22, y + 44, 16, 2, color);
    lcd_fill_rect(x + 18, y + 49, 24, 2, color);
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
    else if(app->screen == SCREEN_CAMERA)
        menu_draw_camera_icon(x, y, fg);
    else if(app->screen == SCREEN_QR_CAMERA)
        menu_draw_qr_camera_icon(x, y, fg);
    else if(app->screen == SCREEN_FILES)
        menu_draw_files_icon(x, y, fg, bg);
    else if(app->screen == SCREEN_BUTTONS)
        menu_draw_buttons_icon(x, y, fg);
    else if(app->screen == SCREEN_SETTINGS)
        menu_draw_settings_icon(x, y, fg);
    else if(app->screen == SCREEN_SLEEP)
        menu_draw_sleep_icon(x, y, fg);
}
