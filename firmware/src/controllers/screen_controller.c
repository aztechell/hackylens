#include "screen_controller.h"

#include "../core/hk_app.h"

#include "camera_settings_coordinator.h"
#include "hk_config.h"

uint8_t screen_controller_handle_input(screen_t screen, const hk_input_snapshot_t *input)
{
#if HK_ENABLE_APP_CAMERA || HK_ENABLE_APP_QR_CAMERA
    if(screen == SCREEN_CAMERA_SETTINGS)
    {
        camera_settings_coordinator_handle_buttons(input);
        return 1;
    }
#else
    (void)input;
#endif
    (void)screen;
    return 0;
}
