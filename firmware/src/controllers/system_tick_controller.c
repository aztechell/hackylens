#include "system_tick_controller.h"

#include "../core/hk_app.h"
#include "../core/hk_app_registry.h"
#include "../core/hk_events.h"

#include "../core/hk_dispatch.h"
#include "../core/hk_screen.h"
#include "../services/settings_persistence.h"
#include "../services/external_link_service.h"
#include "../services/sd_service.h"
#include "auto_sleep_controller.h"
#include "hk_config.h"
#if HK_ENABLE_CAMERA_FEATURE
#include "../services/camera_session.h"
#endif
#if HK_ENABLE_APP_MICROPYTHON
#include "../adapters/micropython/micropython_capability_bridge.h"
#include "../services/micropython_runtime.h"
#endif
#if HK_ENABLE_APP_QR_CAMERA
#include "../services/qr_debug_service.h"
#endif
#if HK_ENABLE_APP_FILES
#include "../services/files_poll_service.h"
#endif

void system_tick_controller_tick(const hk_input_snapshot_t *input)
{
#if HK_ENABLE_APP_MICROPYTHON
    micropython_capability_bridge_tick();
    micropython_runtime_poll();
#endif
    external_link_service_tick();
    auto_sleep_controller_tick(input);
    hk_app_registry_background_tick(input);
    if(hk_app_registry_sd_poll_allowed(hk_screen_get()) &&
       !sleep_session_active()
#if HK_ENABLE_CAMERA_FEATURE
       && !camera_session_blocks_sd_poll()
#endif
      )
    {
        hk_sd_event_t sd_event = sd_service_tick();
        if(sd_event != HK_SD_EVENT_NONE)
            shell_handle_sd_event(sd_event);
    }
    settings_storage_tick();
#if HK_ENABLE_APP_QR_CAMERA
    qr_camera_poll_decode();
#endif
#if HK_ENABLE_APP_FILES
    files_poll_animation();
#endif
}
