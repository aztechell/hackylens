#include "settings_controller.h"

#include <string.h>

static void settings_open_page(settings_state_t *state, settings_page_t page)
{
    const settings_menu_definition_t *definition = page == SETTINGS_PAGE_STARTUP ?
        settings_app_startup_choices(&state->startup) : settings_app_page_definition(page);
    if(definition && settings_menu_bind(&state->menu, definition))
        state->page = page;
}

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

    if(!state || !event || !settings_menu_active(&state->menu))
        return;
    if((event->pressed & (HK_INPUT_BUTTON_OK | HK_INPUT_BUTTON_BACK)) == HK_INPUT_BUTTON_OK)
    {
        if(state->page == SETTINGS_PAGE_ROOT)
        {
            const settings_menu_item_t *item = &state->menu.definition->items[state->menu.index];
            if(item->kind == SETTINGS_MENU_ITEM_ACTION)
            {
                state->root_index = state->menu.index;
                settings_open_page(state, (settings_page_t)item->id);
                state->dirty = 1U;
                return;
            }
        }
        if(state->page == SETTINGS_PAGE_LIGHTS &&
           state->menu.definition->items[state->menu.index].id == SETTINGS_PAGE_RGB)
        {
            state->parent_index = state->menu.index;
            settings_open_page(state, SETTINGS_PAGE_RGB);
            state->dirty = 1U;
            return;
        }
    }
    snapshot = snapshot_from_event(event);
    if(settings_menu_handle_input(&state->menu, &snapshot) ==
       SETTINGS_MENU_EVENT_CLOSE_REQUESTED)
    {
        if(state->page == SETTINGS_PAGE_ROOT)
            state->close_requested = 1U;
        else if(state->page == SETTINGS_PAGE_RGB)
        {
            settings_open_page(state, SETTINGS_PAGE_LIGHTS);
            state->menu.index = state->parent_index;
        }
        else
        {
            settings_open_page(state, SETTINGS_PAGE_ROOT);
            state->menu.index = state->root_index;
        }
    }
    if(state->page == SETTINGS_PAGE_CONNECTION && !state->menu.editing &&
       state->menu.definition != settings_app_page_definition(SETTINGS_PAGE_CONNECTION))
        settings_open_page(state, SETTINGS_PAGE_CONNECTION);
    state->dirty = 1U;
}

void settings_controller_tick(settings_state_t *state, uint32_t buttons)
{
    hk_input_snapshot_t snapshot = {buttons, 0U, 0U};

    if(!state)
        return;
    int32_t previous = state->menu.edit_value;
    settings_menu_tick(&state->menu, &snapshot);
    if(previous != state->menu.edit_value)
        state->dirty = 1U;
}
