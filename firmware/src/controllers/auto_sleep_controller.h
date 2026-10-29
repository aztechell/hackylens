#ifndef HK_AUTO_SLEEP_CONTROLLER_H
#define HK_AUTO_SLEEP_CONTROLLER_H

#include "../core/hk_app.h"

uint8_t auto_sleep_take_request(void);
void auto_sleep_controller_tick(const hk_input_snapshot_t *input);
void sleep_session_set_active(uint8_t active);
uint8_t sleep_session_active(void);

#endif
