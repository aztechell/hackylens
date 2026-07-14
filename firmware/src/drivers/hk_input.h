#ifndef HK_INPUT_H
#define HK_INPUT_H

#include "../core/hk_app.h"


void buttons_sync(void);
void buttons_poll(void);
uint32_t hk_input_state(void);
uint32_t hk_input_pressed(void);
uint32_t hk_input_changed(void);
hk_input_snapshot_t hk_input_current(void);
hk_input_snapshot_t hk_input_poll(void);

#endif
