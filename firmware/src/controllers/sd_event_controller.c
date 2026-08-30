#include "sd_event_controller.h"

#include "../core/hk_app.h"
#include "../core/hk_app_registry.h"
#include "../core/hk_events.h"

#include "../core/hk_menu_runtime.h"
#include "../core/hk_screen.h"
#include "../storage/fat32_volume.h"
#include "../ui/hk_ui.h"

static sd_event_app_hook_t s_app_hook;

void sd_event_controller_set_app_hook(sd_event_app_hook_t hook)
{
    s_app_hook = hook;
}

void sd_event_controller_handle(hk_sd_event_t event)
{
    if(event == HK_SD_EVENT_NONE)
        return;

    if(s_app_hook && !s_app_hook(event) &&
       hk_screen_get() == SCREEN_APP_SLOT_0)
        shell_show_menu_reason(HK_APP_STOP_CALLBACK_FAILED);

    hk_app_registry_handle_sd_event(event);
    topbar_set_sd_mounted((uint8_t)(hk_sd_present() && hk_fat_mounted()));
    if(hk_screen_get() == SCREEN_MENU)
        menu_render();
}
