#ifndef CAMERA_PHOTO_CONTROLLER_H
#define CAMERA_PHOTO_CONTROLLER_H

#include <stdint.h>

typedef void (*camera_photo_preview_redraw_t)(void);

uint8_t camera_photo_controller_active(void);
void camera_photo_controller_take(void);
void camera_photo_controller_tick(camera_photo_preview_redraw_t redraw_preview);
void camera_photo_controller_dismiss(void);

#endif
