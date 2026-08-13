#include "firmware_startup.h"

#include <stdio.h>

#include "../../../platforms/k210/startup/platform_bootstrap.h"

#include "../controllers/boot_controller.h"
#include "../controllers/autostart_controller.h"
#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "../services/settings_lights.h"
#include "../services/debug_console_service.h"
#include "../services/external_link_service.h"
#include "../services/settings_persistence.h"
#include "../services/settings_service.h"
#include "../storage/file_mount.h"
#include "../ui/hk_ui.h"

static void firmware_wake_from_sleep(void)
{
    if(hk_screen_get() != SCREEN_SLEEP)
        return;
    screen_brightness_apply();
    illum_led_apply();
    rgb_led_apply();
    printf("[SLEEP] wake\r\n");
    shell_show_menu();
}

void firmware_startup(void)
{
    debug_console_init();
    hk_screen_set_wake_handler(firmware_wake_from_sleep);
    platform_bootstrap_init_clocks();
    settings_storage_init();
    platform_bootstrap_init_hardware();
    external_link_service_init(settings_external_link_transport());
    screen_brightness_apply();
    illum_led_apply();
    rgb_led_apply();
    boot_controller_startup();
    boot_controller_show_boot_screen();
    topbar_set_sd_mounted(file_mount_if_needed());
    autostart_controller_start();
    debug_console_start_rx();
}
