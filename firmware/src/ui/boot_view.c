#include "boot_view.h"

#include "../drivers/hk_lcd.h"

void boot_view_show_logo(void)
{
    lcd_draw_boot_logo();
}
