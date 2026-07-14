#include "camera_status_view.h"

#include "../config/display_config.h"
#include "../config/menu_layout.h"

#include "../drivers/hk_lcd.h"

void camera_status_view_draw(const char *line1, const char *line2)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COLOR_BLACK);
    lcd_draw_rect(0, 0, LCD_W, LCD_H, MENU_LINE, COLOR_TERM_GREEN);
    lcd_draw_text_centered(78, line1, COLOR_TERM_GREEN, COLOR_BLACK);
    lcd_draw_text_centered(108, line2, COLOR_TERM_GREEN, COLOR_BLACK);
}
