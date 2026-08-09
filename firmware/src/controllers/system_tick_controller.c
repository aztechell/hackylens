#include "system_tick_controller.h"

#include "../core/hk_app.h"
#include "../core/hk_app_registry.h"
#include "../core/hk_events.h"

#include "../core/hk_dispatch.h"
#include "../core/hk_screen.h"
#include "../services/settings_persistence.h"
#include "../services/external_link_service.h"
#include "../services/sd_service.h"
#include "hk_config.h"
#if HK_ENABLE_APP_MICROPYTHON
#include "../services/micropython_binding_service.h"
#include "../services/micropython_runtime.h"
#endif
void system_tick_controller_tick(const hk_input_snapshot_t *input)
{
#if HK_ENABLE_APP_MICROPYTHON
    micropython_binding_service_tick();
    micropython_runtime_poll();
#endif
    external_link_service_tick();
    hk_app_registry_background_tick(input);
    if(hk_app_registry_sd_poll_allowed(hk_screen_get()))
    {
        hk_sd_event_t sd_event = sd_service_tick();
        if(sd_event != HK_SD_EVENT_NONE)
            shell_handle_sd_event(sd_event);
    }
    settings_storage_tick();
}
