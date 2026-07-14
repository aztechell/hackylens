#include "app_qr_camera.h"

#include "../core/hk_app.h"

#include "../controllers/qr_camera_mode_controller.h"

void qr_camera_enter(const hk_input_snapshot_t *input)
{
    qr_camera_mode_enter(input);
}

void qr_camera_tick(const hk_input_snapshot_t *input)
{
    qr_camera_mode_tick(input);
}

void qr_camera_handle_buttons(const hk_input_snapshot_t *input)
{
    qr_camera_mode_handle_buttons(input);
}
