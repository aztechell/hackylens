#include "firmware_startup.h"

#include "platform_bootstrap.h"

#include "../controllers/boot_controller.h"
#include "../core/hk_menu.h"
#include "../services/settings_lights.h"
#include "../services/settings_persistence.h"
#include "../storage/file_mount.h"
#include "../ui/hk_ui.h"

void firmware_startup(void)
{
    platform_bootstrap_init_clocks();
    settings_storage_init();
    platform_bootstrap_init_hardware();
    screen_brightness_apply();
    illum_led_apply();
    rgb_led_apply();
    boot_controller_startup();
    boot_controller_show_boot_screen();
    topbar_set_sd_mounted(files_mount_if_needed());
    shell_show_menu();
}
