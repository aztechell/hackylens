#include "camera_settings_coordinator.h"

#include "../services/camera_session.h"
#include "camera_photo_mode_controller.h"
#include "camera_settings_controller.h"
#include "qr_camera_mode_controller.h"

void camera_settings_coordinator_handle_buttons(const hk_input_snapshot_t *input)
{
    camera_settings_exit_t exit = camera_settings_handle_buttons(input);

    if(exit == CAMERA_SETTINGS_EXIT_QR)
    {
        qr_camera_mode_resume();
        return;
    }

    if(exit == CAMERA_SETTINGS_REINIT_PHOTO)
    {
        hk_input_snapshot_t restart_input = {input ? input->state : 0, 0, 0};

        camera_stop();
        camera_photo_mode_enter(&restart_input);
        return;
    }

    if(exit == CAMERA_SETTINGS_EXIT_PHOTO)
        camera_photo_mode_resume();
}
