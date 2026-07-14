#include "sleep_view.h"

#include "../config/display_config.h"
#include "../drivers/hk_lcd.h"
#include "hk_ui.h"

void sleep_view_enter(void)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COLOR_BLACK);
}
