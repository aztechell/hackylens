#include "buttons_view.h"

#include <stdio.h>

#include "../../config/display_config.h"
#include "../../config/input_config.h"
#include "../../drivers/hk_lcd.h"
#include "../../ui/hk_ui.h"

static void buttons_view_draw_row(uint16_t y, const char *name, uint32_t mask, uint32_t state)
{
    char line[32];

    snprintf(line, sizeof(line), "%-5s %s", name, (state & mask) ? "DOWN" : "UP");
    lcd_fill_rect(12, y, LCD_W - 24, HACKYLENS_FONT_H, COLOR_BLACK);
    lcd_draw_text_at(12, y, line, COLOR_TERM_GREEN, COLOR_BLACK);
}

void buttons_view_render(uint32_t state)
{
    menu_draw_chrome("BUTTONS");
    buttons_view_draw_row(48, "LEFT", BUTTON_LEFT, state);
    buttons_view_draw_row(78, "RIGHT", BUTTON_RIGHT, state);
    buttons_view_draw_row(108, "OK", BUTTON_OK, state);
    buttons_view_draw_row(138, "BACK", BUTTON_BACK, state);
}

void buttons_view_update(uint32_t changed, uint32_t state)
{
    if(changed & BUTTON_LEFT)
        buttons_view_draw_row(48, "LEFT", BUTTON_LEFT, state);
    if(changed & BUTTON_RIGHT)
        buttons_view_draw_row(78, "RIGHT", BUTTON_RIGHT, state);
    if(changed & BUTTON_OK)
        buttons_view_draw_row(108, "OK", BUTTON_OK, state);
    if(changed & BUTTON_BACK)
        buttons_view_draw_row(138, "BACK", BUTTON_BACK, state);
}

void buttons_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    lcd_draw_rect(x + 13, y + 12, 14, 14, 2, color);
    lcd_draw_rect(x + 33, y + 12, 14, 14, 2, color);
    lcd_draw_rect(x + 13, y + 34, 14, 14, 2, color);
    lcd_draw_rect(x + 33, y + 34, 14, 14, 2, color);
}
