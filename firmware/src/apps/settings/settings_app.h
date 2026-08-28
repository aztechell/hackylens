#ifndef HK_SETTINGS_APP_H
#define HK_SETTINGS_APP_H

#include "../../core/hk_app.h"

void settings_enter(const hk_input_snapshot_t *input);
void settings_exit(void);
void settings_tick(const hk_input_snapshot_t *input);
void settings_handle_buttons(const hk_input_snapshot_t *input);
uint8_t settings_handle_debug_command(const char *cmd);
void settings_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);
extern const hk_legacy_app_entry_t settings_legacy_entry;

#endif
