#include "camera_photo_mode_controller.h"

#include <stdio.h>

#include "../core/hk_screen.h"
#include "../services/camera_session.h"
#include "../ui/camera_view.h"
#include "camera_photo_controller.h"
#include "camera_runtime_controller.h"
#include "camera_settings_controller.h"

void camera_photo_mode_enter(const hk_input_snapshot_t *input)
{
    camera_runtime_enter(CAMERA_RUNTIME_PHOTO, input);
}

void camera_photo_mode_tick(const hk_input_snapshot_t *input)
{
    if(camera_runtime_ok_hold_triggered(input))
    {
        camera_service_freeze(1);
        camera_settings_enter();
        return;
    }

    camera_runtime_tick(input);
}

void camera_photo_mode_handle_buttons(const hk_input_snapshot_t *input)
{
    if(camera_runtime_handle_input(input) == CAMERA_RUNTIME_INPUT_OK_RELEASE)
        camera_photo_controller_take(camera_runtime_redraw_preview);
}

void camera_photo_mode_resume(void)
{
    hk_screen_set(SCREEN_CAMERA);
    camera_view_clear();
    printf("[SHELL] screen CAMERA\r\n");
}
