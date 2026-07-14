#ifndef APP_CAMERA_H
#define APP_CAMERA_H

#include "../core/hk_app.h"

void camera_enter(const hk_input_snapshot_t *input);
void camera_tick(const hk_input_snapshot_t *input);
void camera_handle_buttons(const hk_input_snapshot_t *input);

#endif
