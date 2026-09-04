#include "settings_controller.h"

#include <string.h>

static hk_input_snapshot_t snapshot_from_event(const hk_input_event_t *event)
{
    hk_input_snapshot_t snapshot = {0};

    snapshot.state = event->state;
    snapshot.pressed = event->pressed;
    snapshot.changed = event->changed;
    return snapshot;
}

void settings_controller_reset(
    settings_state_t *state, const settings_menu_definition_t *definition)
{
    if(!state)
        return;
    memset(state, 0, sizeof(*state));
    (void)settings_menu_bind(&state->menu, definition);
    state->dirty = 1U;
}

void settings_controller_exit(settings_state_t *state)
{
    if(!state)
        return;
    settings_menu_close(&state->menu);
}

void settings_controller_handle_input(
    settings_state_t *state, const hk_input_event_t *event)
{
    hk_input_snapshot_t snapshot;

    if(!state || !event)
        return;
    snapshot = snapshot_from_event(event);
    if(settings_menu_handle_input(&state->menu, &snapshot) ==
       SETTINGS_MENU_EVENT_CLOSE_REQUESTED)
        state->close_requested = 1U;
    state->dirty = 1U;
}

void settings_controller_tick(settings_state_t *state, uint32_t buttons)
{
    hk_input_snapshot_t snapshot = {buttons, 0U, 0U};

    if(!state)
        return;
    settings_menu_tick(&state->menu, &snapshot);
    if(state->menu.editing)
        state->dirty = 1U;
}
