#include "app_face_detect.h"

#include "../controllers/face_detect_controller.h"

void face_detect_enter(const hk_input_snapshot_t *input) { face_detect_controller_enter(input); }
void face_detect_tick(const hk_input_snapshot_t *input) { face_detect_controller_tick(input); }
void face_detect_handle_buttons(const hk_input_snapshot_t *input) { face_detect_controller_handle_buttons(input); }
