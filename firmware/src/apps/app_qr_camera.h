#ifndef APP_QR_CAMERA_H
#define APP_QR_CAMERA_H

#include "../core/hk_app.h"

void qr_camera_enter(const hk_input_snapshot_t *input);
void qr_camera_tick(const hk_input_snapshot_t *input);
void qr_camera_handle_buttons(const hk_input_snapshot_t *input);

#endif
