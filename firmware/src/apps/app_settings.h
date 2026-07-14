#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "../core/hk_app.h"

void settings_enter(const hk_input_snapshot_t *input);
void settings_tick(const hk_input_snapshot_t *input);
void settings_handle_buttons(const hk_input_snapshot_t *input);

#endif
