#ifndef HK_APP_FACE_DETECT_H
#define HK_APP_FACE_DETECT_H

#include "../core/hk_app.h"

void face_detect_enter(const hk_input_snapshot_t *input);
void face_detect_tick(const hk_input_snapshot_t *input);
void face_detect_handle_buttons(const hk_input_snapshot_t *input);

#endif
