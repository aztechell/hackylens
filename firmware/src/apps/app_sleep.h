#ifndef APP_SLEEP_H
#define APP_SLEEP_H

#include "../core/hk_app.h"

void sleep_enter(const hk_input_snapshot_t *input);
void sleep_handle_buttons(const hk_input_snapshot_t *input);

#endif
