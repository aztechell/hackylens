#ifndef SETTINGS_CONTROLLER_H
#define SETTINGS_CONTROLLER_H

#include <hackylens/app.h>

#include "settings_menu.h"

typedef struct
{
    settings_menu_session_t menu;
    hk_owner_t owner;
    hk_input_t input;
    uint8_t close_requested;
    uint8_t dirty;
} settings_state_t;

void settings_controller_reset(
    settings_state_t *state, const settings_menu_definition_t *definition);
void settings_controller_exit(settings_state_t *state);
void settings_controller_handle_input(
    settings_state_t *state, const hk_input_event_t *event);
void settings_controller_tick(settings_state_t *state, uint32_t buttons);

#endif
