#ifndef HK_QR_CAMERA_MODE_CONTROLLER_H
#define HK_QR_CAMERA_MODE_CONTROLLER_H

#include "../core/hk_app.h"

void qr_camera_mode_enter(const hk_input_snapshot_t *input);
void qr_camera_mode_tick(const hk_input_snapshot_t *input);
void qr_camera_mode_handle_buttons(const hk_input_snapshot_t *input);
void qr_camera_mode_resume(void);

#endif
