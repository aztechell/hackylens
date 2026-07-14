#include "platform_bootstrap.h"

#include <stdio.h>

#include "../board/board_hackylens.h"
#include "../drivers/hk_input.h"
#include "../drivers/hk_lcd.h"
#include "../hal/hal_system.h"

void platform_bootstrap_init_clocks(void)
{
    hal_system_init_clocks();
}

void platform_bootstrap_init_hardware(void)
{
    board_lcd_init_original();
    board_leds_init_candidate();
    board_buttons_init();
    buttons_sync();

    printf("[LCD] init original sequence\r\n");
    lcd_init_original_sequence();
}
