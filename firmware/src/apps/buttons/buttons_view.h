#ifndef BUTTONS_VIEW_H
#define BUTTONS_VIEW_H

#include <stdint.h>

void buttons_view_render(uint32_t state);
void buttons_view_update(uint32_t changed, uint32_t state);
void buttons_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);

#endif
