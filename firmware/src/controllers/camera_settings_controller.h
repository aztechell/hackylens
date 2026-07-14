#ifndef CAMERA_SETTINGS_CONTROLLER_H
#define CAMERA_SETTINGS_CONTROLLER_H

#include "../core/hk_app.h"

typedef enum
{
    CAMERA_SETTINGS_EXIT_NONE = 0,
    CAMERA_SETTINGS_EXIT_PHOTO,
    CAMERA_SETTINGS_EXIT_QR,
    CAMERA_SETTINGS_REINIT_PHOTO,
} camera_settings_exit_t;

void camera_settings_enter(void);
void qr_settings_enter(void);
camera_settings_exit_t camera_settings_handle_buttons(const hk_input_snapshot_t *input);

#endif
