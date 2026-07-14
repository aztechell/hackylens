#ifndef PONG_CONTROLLER_H
#define PONG_CONTROLLER_H

#include "../../core/hk_app.h"

void pong_controller_enter(const hk_input_snapshot_t *input);
void pong_controller_tick(const hk_input_snapshot_t *input);
void pong_controller_handle_buttons(const hk_input_snapshot_t *input);

#endif
