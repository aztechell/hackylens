#ifndef HK_CAMERA_RUNTIME_CONTROLLER_H
#define HK_CAMERA_RUNTIME_CONTROLLER_H

#include "../core/hk_app.h"

typedef enum
{
    CAMERA_RUNTIME_PHOTO = 0,
    CAMERA_RUNTIME_QR,
    CAMERA_RUNTIME_FACE_DETECT,
} camera_runtime_mode_t;

typedef enum
{
    CAMERA_RUNTIME_INPUT_NONE = 0,
    CAMERA_RUNTIME_INPUT_EXIT,
    CAMERA_RUNTIME_INPUT_OK_RELEASE,
} camera_runtime_input_event_t;

void camera_runtime_enter(camera_runtime_mode_t mode, const hk_input_snapshot_t *input);
uint8_t camera_runtime_tick(const hk_input_snapshot_t *input);
camera_runtime_input_event_t camera_runtime_handle_input(const hk_input_snapshot_t *input);
uint8_t camera_runtime_ok_hold_triggered(const hk_input_snapshot_t *input);
void camera_runtime_redraw_preview(void);

#endif
