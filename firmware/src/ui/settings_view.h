#ifndef SETTINGS_VIEW_H
#define SETTINGS_VIEW_H

#include <stdint.h>

typedef struct
{
    const char *title;
    char value[12];
} settings_view_row_t;

typedef struct
{
    const settings_view_row_t *rows;
    uint8_t row_count;
    uint8_t index;
    uint8_t top;
} settings_view_model_t;

const char *settings_view_row_title(uint8_t index);
void settings_view_render(const settings_view_model_t *model);
void settings_view_draw_row(const settings_view_model_t *model, uint8_t index);

#endif
