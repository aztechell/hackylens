#include "settings_controller.h"

#include <string.h>

static void settings_open_page(settings_state_t *state, settings_page_t page)
{
    if(page == SETTINGS_PAGE_MENU_ORDER)
    {
        settings_order_editor_t *order = &state->order;
        uint8_t shortcuts = 0U;

        memset(order, 0, sizeof(*order));
        order->count = hk_menu_get_items(order->items);
        for(uint8_t index = 0; index < order->count; index++)
        {
            hk_menu_item_t *item = &order->items[index];
            if(item->shortcut && shortcuts < HK_MENU_SHORTCUT_CAPACITY)
            {
                size_t length = strlen(item->shortcut) + 1U;
                memcpy(order->shortcut_names[shortcuts], item->shortcut, length);
                item->shortcut = order->shortcut_names[shortcuts++];
            }
        }
        state->page = page;
        return;
    }
    const settings_menu_definition_t *definition = page == SETTINGS_PAGE_STARTUP ?
        settings_app_startup_choices(&state->startup) : settings_app_page_definition(page);
    if(definition && settings_menu_bind(&state->menu, definition))
        state->page = page;
}

static void settings_move_icon(settings_order_editor_t *order, uint8_t to)
{
    uint8_t from = order->index;
    hk_menu_item_t picked = order->items[from];

    if(to < from)
        memmove(&order->items[to + 1U], &order->items[to],
                (from - to) * sizeof(picked));
    else if(to > from)
        memmove(&order->items[from], &order->items[from + 1U],
                (to - from) * sizeof(picked));
    order->items[to] = picked;
    order->index = to;
}

static void settings_order_scroll(settings_order_editor_t *order)
{
    if(order->index < order->top)
        order->top = order->index;
    else if(order->index >= order->top + SETTINGS_ORDER_VISIBLE_ROWS)
        order->top = order->index - SETTINGS_ORDER_VISIBLE_ROWS + 1U;
}

static uint8_t settings_order_step(settings_order_editor_t *order, uint32_t button)
{
    uint8_t previous = order->index;
    int next = previous + (button == HK_INPUT_BUTTON_LEFT ? -1 : 1);

    if(order->moving)
    {
        if(next >= 0 && next < order->count)
            settings_move_icon(order, (uint8_t)next);
    }
    else
        order->index = (uint8_t)((next + order->count) % order->count);
    return order->index != previous;
}

static void settings_order_handle_input(settings_state_t *state, uint32_t pressed)
{
    settings_order_editor_t *order = &state->order;

    if(pressed & HK_INPUT_BUTTON_BACK)
    {
        order->repeat_button = 0U;
        if(!order->moving)
        {
            state->page = SETTINGS_PAGE_ROOT;
            return;
        }
        settings_move_icon(order, order->original_index);
        order->moving = 0U;
    }
    else if(!order->count)
        return;
    else if(pressed & HK_INPUT_BUTTON_OK)
    {
        order->repeat_button = 0U;
        if(order->moving)
        {
            if(order->index != order->original_index)
                hk_menu_save_order(order->items, order->count);
            order->moving = 0U;
        }
        else
        {
            order->original_index = order->index;
            order->moving = 1U;
        }
    }
    else if(pressed & (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_RIGHT))
    {
        uint32_t button = (pressed & HK_INPUT_BUTTON_LEFT) ?
                          HK_INPUT_BUTTON_LEFT : HK_INPUT_BUTTON_RIGHT;
        if(order->moving)
        {
            order->repeat_button = button;
            order->repeat_ticks = SETTINGS_MENU_REPEAT_INITIAL_TICKS;
        }
        (void)settings_order_step(order, button);
    }

    settings_order_scroll(order);
}

static void settings_order_tick(settings_state_t *state, uint32_t buttons)
{
    settings_order_editor_t *order = &state->order;

    if(!(buttons & order->repeat_button))
    {
        order->repeat_button = 0U;
        return;
    }
    if(--order->repeat_ticks != 0U)
        return;

    order->repeat_ticks = SETTINGS_MENU_REPEAT_NEXT_TICKS;
    if(settings_order_step(order, order->repeat_button))
    {
        settings_order_scroll(order);
        state->dirty = 1U;
    }
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
    if(state->page == SETTINGS_PAGE_MENU_ORDER)
    {
        if(event->pressed)
        {
            settings_order_handle_input(state, event->pressed);
            state->dirty = 1U;
        }
        return;
    }
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
    if(state->page == SETTINGS_PAGE_MENU_ORDER)
    {
        settings_order_tick(state, buttons);
        return;
    }
    int32_t previous = state->menu.edit_value;
    settings_menu_tick(&state->menu, &snapshot);
    if(previous != state->menu.edit_value)
        state->dirty = 1U;
}
