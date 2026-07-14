#ifndef SETTINGS_MODEL_H
#define SETTINGS_MODEL_H

#include <stdint.h>

const char *settings_model_row_title(uint8_t index);
void settings_model_ensure_visible(uint8_t index, uint8_t *top);
void settings_model_render(uint8_t index, uint8_t top, uint8_t editing);
void settings_model_draw_row(uint8_t index, uint8_t top, uint8_t editing, uint8_t row_index);

#endif
