#ifndef HK_CAMERA_PHOTO_MODE_CONTROLLER_H
#define HK_CAMERA_PHOTO_MODE_CONTROLLER_H

#include "../core/hk_app.h"

void camera_photo_mode_enter(const hk_input_snapshot_t *input);
void camera_photo_mode_tick(const hk_input_snapshot_t *input);
void camera_photo_mode_handle_buttons(const hk_input_snapshot_t *input);
void camera_photo_mode_resume(void);

#endif
