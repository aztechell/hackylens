#ifndef HK_BUTTONS_APP_H
#define HK_BUTTONS_APP_H

#include "../../core/hk_app.h"

void buttons_enter(const hk_input_snapshot_t *input);
void buttons_tick(const hk_input_snapshot_t *input);
void buttons_handle_buttons(const hk_input_snapshot_t *input);
void buttons_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);
extern const hk_legacy_app_entry_t buttons_legacy_entry;

#endif
