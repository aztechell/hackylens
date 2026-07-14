#include "app_camera.h"

#include "../core/hk_app.h"

#include "../controllers/camera_photo_mode_controller.h"

void camera_enter(const hk_input_snapshot_t *input)
{
    camera_photo_mode_enter(input);
}

void camera_tick(const hk_input_snapshot_t *input)
{
    camera_photo_mode_tick(input);
}

void camera_handle_buttons(const hk_input_snapshot_t *input)
{
    camera_photo_mode_handle_buttons(input);
}
