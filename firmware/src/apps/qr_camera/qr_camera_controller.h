#ifndef HK_QR_CAMERA_CONTROLLER_H
#define HK_QR_CAMERA_CONTROLLER_H

#include <hackylens/app.h>
#include "../../core/hk_app.h"

typedef struct
{
    hk_owner_t owner;
    hk_input_t input;
    uint8_t close_requested;
} qr_camera_state_t;

void qr_camera_controller_reset(qr_camera_state_t *state);
void qr_camera_controller_enter(qr_camera_state_t *state);
void qr_camera_controller_exit(qr_camera_state_t *state);
void qr_camera_controller_handle_input(
    qr_camera_state_t *state, const hk_input_event_t *event);
void qr_camera_controller_tick(const hk_input_snapshot_t *input);
void qr_camera_controller_poll_decode(void);
uint8_t qr_camera_controller_settings_active(void);
uint8_t qr_camera_session_active(void);

#endif
