#ifndef SETTINGS_CONTROLLER_H
#define SETTINGS_CONTROLLER_H

#include <hackylens/app.h>

#include "settings_menu.h"

typedef struct
{
    settings_menu_session_t menu;
    settings_startup_choices_t startup;
    const hk_input_t *input;
    uint8_t close_requested;
    uint8_t dirty;
    settings_page_t page;
    uint8_t root_index;
    uint8_t parent_index;
    uint8_t sd_usage_status;
    uint8_t sd_usage_progress;
    uint64_t sd_used_bytes;
    uint64_t sd_total_bytes;
} settings_state_t;

void settings_controller_reset(
    settings_state_t *state, const settings_menu_definition_t *definition);
void settings_controller_exit(settings_state_t *state);
void settings_controller_handle_input(
    settings_state_t *state, const hk_input_event_t *event);
void settings_controller_tick(settings_state_t *state, uint32_t buttons);

#endif
