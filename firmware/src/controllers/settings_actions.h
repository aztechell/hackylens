#ifndef SETTINGS_ACTIONS_H
#define SETTINGS_ACTIONS_H

#include <stdint.h>

#define SETTINGS_ACTION_NO_ROW 0xFFU

uint8_t settings_action_is_editable_row(uint8_t index);
uint8_t settings_action_toggle_led(void);
uint8_t settings_action_toggle_rgb(void);
uint8_t settings_action_adjust_value(uint8_t index, int8_t delta);

#endif
