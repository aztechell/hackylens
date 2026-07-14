#ifndef CAMERA_SETTINGS_VIEW_H
#define CAMERA_SETTINGS_VIEW_H

#include <stdint.h>

typedef struct
{
    const char *title;
    char value[24];
} camera_settings_view_row_t;

typedef struct
{
    const char *title;
    const camera_settings_view_row_t *rows;
    uint8_t row_count;
    uint8_t index;
    uint8_t top;
} camera_settings_view_model_t;

void camera_settings_view_render(const camera_settings_view_model_t *model);
void camera_settings_view_draw_row(const camera_settings_view_model_t *model, uint8_t index);

#endif
