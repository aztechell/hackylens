#include "system_tick_controller.h"

#include "../core/hk_app.h"
#include "../core/hk_app_registry.h"
#include "../core/hk_events.h"

#include "hk_config.h"

#include "../core/hk_dispatch.h"
#include "../core/hk_screen.h"
#include "../services/settings_persistence.h"
#include "../services/external_link_service.h"
#include "../services/sd_service.h"
#include "sleep_controller.h"

void system_tick_controller_tick(const hk_input_snapshot_t *input)
{
    external_link_service_tick();
    hk_app_registry_background_tick();
#if HK_ENABLE_APP_SLEEP
    auto_sleep_controller_tick(input);
#endif
    if(hk_screen_get() != SCREEN_CAMERA && hk_screen_get() != SCREEN_QR_CAMERA && hk_screen_get() != SCREEN_FACE_DETECT && hk_screen_get() != SCREEN_SLEEP)
    {
        hk_sd_event_t sd_event = sd_service_tick();
        if(sd_event != HK_SD_EVENT_NONE)
            shell_handle_sd_event(sd_event);
    }
    settings_storage_tick();
}
