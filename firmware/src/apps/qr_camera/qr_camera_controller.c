#include "qr_camera_controller.h"

#include <stdio.h>
#include <string.h>

#include "qr_camera_firmware.h"
#include "qr_camera_frame_adapter.h"
#include "qr_result.h"
#include "qr_result_controller.h"
#include "qr_result_view.h"
#include "qr_service.h"
#include "qr_settings.h"

static uint8_t s_session_active;

static hk_input_snapshot_t snapshot_from_event(const hk_input_event_t *event)
{
    hk_input_snapshot_t snapshot = {0};

    snapshot.state = event->state;
    snapshot.pressed = event->pressed;
    snapshot.changed = event->changed;
    return snapshot;
}

void qr_camera_controller_reset(qr_camera_state_t *state)
{
    if(!state)
        return;
    memset(state, 0, sizeof(*state));
}

void qr_camera_controller_enter(qr_camera_state_t *state)
{
    (void)state;
    s_session_active = 1U;
    qr_service_enter();
    camera_runtime_enter(CAMERA_RUNTIME_QR, NULL);
}

void qr_camera_controller_exit(qr_camera_state_t *state)
{
    (void)state;
    qr_settings_close();
    qr_result_close_window();
    qr_result_reset();
    camera_stop();
    camera_service_clear_mode();
    s_session_active = 0U;
}

void qr_camera_controller_tick(const hk_input_snapshot_t *input)
{
    if(!input)
        return;
    if(qr_settings_active())
    {
        qr_settings_tick(input);
        return;
    }
    if(!qr_result_open() && camera_runtime_ok_hold_triggered(input))
    {
        camera_service_freeze(1U);
        qr_settings_open();
        return;
    }
    if(camera_runtime_tick(input) && !qr_result_open())
    {
        if(qr_service_decode_maybe(0U) == QR_DECODE_FOUND)
        {
            qr_result_show();
            camera_service_freeze(1U);
            qr_result_controller_render();
        }
    }
}

void qr_camera_controller_handle_input(
    qr_camera_state_t *state, const hk_input_event_t *event)
{
    hk_input_snapshot_t input;
    qr_result_input_result_t result_input;

    if(!state || !event)
        return;
    input = snapshot_from_event(event);
    if(qr_settings_active())
    {
        if(qr_settings_handle_input(&input))
        {
            hk_screen_set(SCREEN_APP_SLOT_0);
            camera_view_clear();
            printf("[SHELL] screen QR-CAMERA\r\n");
        }
        return;
    }

    result_input = qr_result_controller_handle_input(input.pressed);
    if(result_input == QR_RESULT_INPUT_CLOSE_REQUEST)
    {
        qr_result_close_window();
        qr_result_view_clear();
        qr_camera_frame_result_close((input.state & HK_INPUT_BUTTON_OK) ? 1U : 0U);
        printf("[QR] result close\r\n");
        return;
    }
    if(result_input == QR_RESULT_INPUT_HANDLED)
        return;
    if(camera_runtime_handle_input(&input) == CAMERA_RUNTIME_INPUT_EXIT)
        state->close_requested = 1U;
}

uint8_t qr_camera_controller_settings_active(void)
{
    return qr_settings_active();
}

uint8_t qr_camera_session_active(void)
{
    return s_session_active;
}
