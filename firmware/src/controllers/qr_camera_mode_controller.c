#include "qr_camera_mode_controller.h"

#include <stdio.h>

#include "../config/input_config.h"

#include "../core/hk_screen.h"
#include "../ui/camera_view.h"
#include "../services/camera_session.h"
#include "../services/qr_camera_frame_adapter.h"
#include "../services/qr_result.h"
#include "../services/qr_service.h"
#include "../ui/qr_result_view.h"
#include "camera_runtime_controller.h"
#include "camera_settings_controller.h"
#include "qr_result_controller.h"

void qr_camera_mode_enter(const hk_input_snapshot_t *input)
{
    qr_service_enter();
    camera_runtime_enter(CAMERA_RUNTIME_QR, input);
}

void qr_camera_mode_tick(const hk_input_snapshot_t *input)
{
    if(!qr_result_open() && camera_runtime_ok_hold_triggered(input))
    {
        camera_service_freeze(1);
        qr_settings_enter();
        return;
    }

    if(camera_runtime_tick(input) && !qr_result_open())
    {
        if(qr_service_decode_maybe(0) == QR_DECODE_FOUND)
        {
            qr_result_show();
            camera_service_freeze(1);
            qr_result_controller_render();
        }
    }
}

void qr_camera_mode_handle_buttons(const hk_input_snapshot_t *input)
{
    qr_result_input_result_t result_input;

    if(!input)
        return;

    result_input = qr_result_controller_handle_input(input->pressed);
    if(result_input == QR_RESULT_INPUT_CLOSE_REQUEST)
    {
        qr_result_close_window();
        qr_result_view_clear();
        qr_camera_frame_result_close((input->state & BUTTON_OK) ? 1 : 0);
        printf("[QR] result close\r\n");
        return;
    }
    if(result_input == QR_RESULT_INPUT_HANDLED)
        return;

    camera_runtime_handle_input(input);
}

void qr_camera_mode_resume(void)
{
    hk_screen_set(SCREEN_QR_CAMERA);
    camera_view_clear();
    printf("[SHELL] screen QR-CAMERA\r\n");
}
