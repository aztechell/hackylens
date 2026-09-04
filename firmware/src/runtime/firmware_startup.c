#include "firmware_startup.h"
#include "capability_owner_runtime.h"

#include <stdio.h>

#include "../../../platforms/k210/startup/platform_bootstrap.h"

#include "../controllers/boot_controller.h"
#include "../controllers/autostart_controller.h"
#include "../controllers/sd_event_controller.h"
#include "app_runtime_integration.h"
#include "../core/hk_menu_runtime.h"
#include "../core/hk_screen.h"
#include "../services/settings_lights.h"
#include "../services/debug_console_service.h"
#include "../services/external_link_service.h"
#include "../services/settings_persistence.h"
#include "../services/settings_service.h"
#include "../storage/file_mount.h"
#include "../ui/hk_ui.h"
#include "../ui/display_binding.h"

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

static uint8_t firmware_app_enter(
    const hk_app_t *app,
    const hk_input_snapshot_t *input)
{
    return (uint8_t)(app_runtime_integration_open(app, input) == HK_OK);
}

static void firmware_app_exit(
    const hk_app_t *app,
    hk_app_stop_reason_t reason)
{
    (void)app;
    (void)app_runtime_integration_close(reason);
}

static uint8_t firmware_app_media_event(hk_sd_event_t event)
{
    static uint32_t generation;
    hk_app_media_kind_t kind;

    generation++;
    if(event == HK_SD_EVENT_INSERTED)
        kind = HK_APP_MEDIA_INSERTED;
    else if(event == HK_SD_EVENT_REMOVED)
        kind = HK_APP_MEDIA_REMOVED;
    else if(event == HK_SD_EVENT_MOUNTED)
        kind = HK_APP_MEDIA_MOUNTED;
    else
        kind = HK_APP_MEDIA_ERROR;
    return (uint8_t)(
        app_runtime_integration_media(kind, generation) == HK_OK);
}

void firmware_startup(void)
{
    static const hk_menu_owner_hooks_t owner_hooks = {
        .enter = firmware_app_enter,
        .exit = firmware_app_exit,
    };

    menu_owner_hooks_set(&owner_hooks);
    sd_event_controller_set_app_hook(firmware_app_media_event);
    debug_console_init();
    hk_screen_set_wake_handler(firmware_wake_from_sleep);
    platform_bootstrap_init_clocks();
    if(capability_owner_runtime_initialize() != HK_OK)
        printf("[CAPABILITY] owner initialization failed\r\n");
    {
        hk_result_t result = app_runtime_integration_initialize();
        if(result != HK_OK)
            printf("[APP] runtime initialization failed result=%d\r\n",
                   (int)result);
    }
    settings_storage_init();
    platform_bootstrap_init_hardware();
    if(hk_ui_display_prepare() != HK_OK)
        printf("[DISPLAY] capability initialization failed\r\n");
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
