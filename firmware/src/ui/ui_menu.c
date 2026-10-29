#include "hk_ui.h"
#include "menu_icons.h"

#include "../core/hk_app.h"
#include "../core/hk_app_registry.h"

#include "../config/display_config.h"
#include "../config/menu_layout.h"
#include "../ui/display_binding.h"

static uint8_t s_topbar_sd_mounted;

void topbar_set_sd_mounted(uint8_t mounted)
{
    s_topbar_sd_mounted = mounted ? 1 : 0;
}

void topbar_draw_sd_status(void)
{
    uint16_t x = HK_DISPLAY_REQUIRED_WIDTH - 23;
    uint16_t y = 7;

    if(!s_topbar_sd_mounted)
        return;

    hk_ui_display_fill_rect(x + 2, y, 10, 2, COLOR_TERM_GREEN);
    hk_ui_display_fill_rect(x + 12, y + 2, 2, 10, COLOR_TERM_GREEN);
    hk_ui_display_fill_rect(x, y + 3, 2, 11, COLOR_TERM_GREEN);
    hk_ui_display_fill_rect(x + 2, y + 12, 12, 2, COLOR_TERM_GREEN);
    hk_ui_display_fill_rect(x + 9, y, 2, 5, COLOR_TERM_GREEN);
    hk_ui_display_fill_rect(x + 11, y + 4, 3, 2, COLOR_TERM_GREEN);
    for(uint8_t i = 0; i < 4; i++)
        hk_ui_display_fill_rect(x + 3 + i * 3, y + 9, 2, 3, COLOR_TERM_GREEN);
}

void menu_draw_chrome(const char *title)
{
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, COLOR_BLACK);
    hk_ui_display_draw_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, MENU_LINE, COLOR_TERM_GREEN);
    hk_ui_display_fill_rect(0, MENU_BAR_H - MENU_LINE, HK_DISPLAY_REQUIRED_WIDTH, MENU_LINE, COLOR_TERM_GREEN);
#if HK_DISPLAY_REQUIRED_WIDTH >= 320
    hk_ui_display_draw_text_at(4, 3, "<o>", COLOR_TERM_GREEN, COLOR_BLACK);
#endif
    hk_ui_display_draw_text_centered(3, title, COLOR_TERM_GREEN, COLOR_BLACK);
    topbar_draw_sd_status();
}

void menu_draw_title(const char *title)
{
    const uint16_t title_x = HK_DISPLAY_REQUIRED_WIDTH >= 320 ? 52U : 2U;
    const uint16_t sd_status_x = HK_DISPLAY_REQUIRED_WIDTH - 23U;

    hk_ui_display_fill_rect(
        title_x, 2U, sd_status_x - title_x,
        MENU_BAR_H - 4U - MENU_LINE, COLOR_BLACK);
    hk_ui_display_draw_text_centered(3, title, COLOR_TERM_GREEN, COLOR_BLACK);
}

void menu_draw_item_at(uint8_t index, const hk_app_t *app, uint8_t selected)
{
    void (*draw_icon)(uint16_t, uint16_t, uint16_t, uint16_t) =
        app ? app->draw_icon : NULL;
    uint8_t col = (uint8_t)(index % MENU_GRID_COLS);
    uint8_t row = (uint8_t)(index / MENU_GRID_COLS);
    uint16_t x = MENU_ICON_X0 + col * (MENU_ICON + MENU_ICON_GAP_X);
    uint16_t y = MENU_ICON_Y0 + row * (MENU_ICON + MENU_ICON_GAP_Y);
    uint16_t fg = selected ? COLOR_BLACK : COLOR_TERM_GREEN;
    uint16_t bg = selected ? COLOR_TERM_GREEN : COLOR_BLACK;

    hk_ui_display_fill_rect(x, y, MENU_ICON, MENU_ICON, bg);
    if(selected)
        hk_ui_display_draw_rect(x + 4, y + 4, MENU_ICON - 8, MENU_ICON - 8, MENU_LINE, COLOR_BLACK);
    else
        hk_ui_display_draw_rect(x, y, MENU_ICON, MENU_ICON, MENU_LINE, COLOR_TERM_GREEN);

    if(draw_icon)
        draw_icon(x, y, fg, bg);
}

void menu_draw_page(uint8_t page, uint8_t pages)
{
    if(pages <= 1U)
        return;
    for(uint8_t index = 0U; index < pages; index++)
    {
        uint16_t x = (HK_DISPLAY_REQUIRED_WIDTH - pages * 12U) / 2U + index * 12U;
        hk_ui_display_draw_rect(x, HK_DISPLAY_REQUIRED_HEIGHT - 10U,
                                8U, 4U, 1U, COLOR_TERM_GREEN);
        if(index == page)
            hk_ui_display_fill_rect(x, HK_DISPLAY_REQUIRED_HEIGHT - 10U,
                                    8U, 4U, COLOR_TERM_GREEN);
    }
}

void buttons_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_menu_icon_draw(x, y, color, bg, hk_menu_icon_buttons);
}

void pong_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_menu_icon_draw(x, y, color, bg, hk_menu_icon_pong);
}

void terminal_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_menu_icon_draw(x, y, color, bg, hk_menu_icon_terminal);
}

void settings_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_menu_icon_draw(x, y, color, bg, hk_menu_icon_settings);
}

void sleep_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    hk_ui_display_draw_rect(x + 20, y + 10, 20, 30, 2, color);
    hk_ui_display_fill_rect(x + 28, y + 6, 4, 14, color);
    hk_ui_display_fill_rect(x + 22, y + 44, 16, 2, color);
    hk_ui_display_fill_rect(x + 18, y + 49, 24, 2, color);
}

void power_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_menu_icon_draw(x, y, color, bg, hk_menu_icon_power);
}

void files_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_menu_icon_draw(x, y, color, bg, hk_menu_icon_files);
}

void qr_camera_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_menu_icon_draw(x, y, color, bg, hk_menu_icon_qr_camera);
}
