#include "boot_controller.h"

#include <stdio.h>

#include "../config/settings_config.h"

#include "../core/hk_app_registry.h"
#include "../core/hk_dispatch.h"
#include "../core/hk_menu.h"
#include "../hal/hal_time.h"
#include "../ui/boot_view.h"
#include "../ui/hk_ui.h"
#include "hk_config.h"
#include "screen_controller.h"
#include "sd_event_controller.h"

static const hk_menu_view_t g_menu_view = {
    .draw_chrome = menu_draw_chrome,
    .draw_title = menu_draw_title,
    .draw_item_at = menu_draw_item_at,
};

static void boot_log_banner(void)
{
    printf("[BOOT] HackyLens 1.0.0 full\r\n");
    printf("[BOOT] modular firmware ready\r\n");
    printf("[LCD] IO18=DC via GPIOHS output bit15, IO22=RST, IO19=SPI0_SS3\r\n");
    printf("[BTN] map LEFT=1 OK=2 RIGHT=4 BACK=8\r\n");
    printf("[LED] illum candidate IO23 PWM2_CH3 default OFF\r\n");
    printf("[RGB] candidate IO32/IO30/IO31 PWM2_CH0/1/2 default OFF\r\n");
    printf("[SD] candidate IO27=SCLK IO28=D0 IO26=D1 IO29=CS\r\n");
    printf("[SHOT] UART command HKSHOT streams BMP screenshot over USB serial\r\n");
    printf("[CAMDBG] HKCAMINFO HKFPS HKCAMPROBE HKCAMREGS HKCAMDVP HKCAMBAR HKFRAME HKCAMERA HKMENU debug commands enabled\r\n");
    printf("[QR] QR-CAMERA uses quirc 640x480 autoscan=1..5Hz 320x240 raw, hold OK=settings, HKQRDECODE=640x480 multipass\r\n");
    printf("[SETTINGS] flash slots 0x%06X/0x%06X\r\n", (unsigned)SETTINGS_FLASH_SLOT0, (unsigned)SETTINGS_FLASH_SLOT1);
    printf("[APP] count=%u\r\n", (unsigned)MENU_ITEM_COUNT);
    for(uint8_t i = 0; i < MENU_ITEM_COUNT; i++)
        printf("[APP] %u title=%s\r\n", (unsigned)i, g_menu_items[i].title);
}

void boot_controller_startup(void)
{
    menu_view_set(&g_menu_view);
    shell_set_screen_input_handler(screen_controller_handle_input);
    shell_set_sd_event_handler(sd_event_controller_handle);

    boot_log_banner();
#if HK_ENABLE_APP_FILES
    files_view_init();
#endif
}

void boot_controller_show_boot_screen(void)
{
    printf("[LCD] draw static boot logo\r\n");
    boot_view_show_logo();
    hal_sleep_ms(1800);
}
