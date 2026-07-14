#ifndef CAMERA_SETTINGS_MODEL_H
#define CAMERA_SETTINGS_MODEL_H

#include <stdint.h>

uint8_t camera_settings_model_row_count(void);
void camera_settings_model_ensure_visible(void);
void camera_settings_model_render(void);
void camera_settings_model_draw_row(uint8_t index);

#endif
