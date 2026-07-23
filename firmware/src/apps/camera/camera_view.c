#include "camera_view.h"

#include "../../drivers/hk_lcd.h"

void camera_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    lcd_draw_rect(x + 10, y + 18, 40, 28, 2, color);
    lcd_fill_rect(x + 18, y + 12, 18, 6, color);
    lcd_draw_rect(x + 23, y + 24, 14, 14, 2, color);
    lcd_fill_rect(x + 28, y + 29, 4, 4, color);
    lcd_fill_rect(x + 43, y + 22, 4, 4, color);
}
