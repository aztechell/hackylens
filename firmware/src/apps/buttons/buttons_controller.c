#include "buttons_controller.h"

#include <string.h>

static const uint32_t s_button_masks[4] = {
    HK_INPUT_BUTTON_LEFT,
    HK_INPUT_BUTTON_OK,
    HK_INPUT_BUTTON_RIGHT,
    HK_INPUT_BUTTON_BACK,
};

static void buttons_controller_refresh_passed(buttons_state_t *state,
                                              uint32_t mask, uint8_t index)
{
    if((state->view.hold_passed & mask) &&
       !(state->view.repeat_error & mask) &&
       state->view.pressed_count[index] != 0U &&
       state->view.pressed_count[index] == state->view.released_count[index])
        state->view.passed |= mask;
    else
        state->view.passed &= ~mask;
}

void buttons_controller_reset(buttons_state_t *state, uint32_t initial_buttons)
{
    uint32_t held = initial_buttons & HK_INPUT_BUTTON_ALL;

    if(!state)
        return;
    memset(state, 0, sizeof(*state));
    state->view.state = held;
    state->ignore_until_released = held;
    state->dirty = 1U;
}

void buttons_controller_handle_input(
    buttons_state_t *state, const hk_input_event_t *event)
{
    uint32_t changed;
    uint8_t index;

    if(!state || !event)
        return;
    changed = event->changed & HK_INPUT_BUTTON_ALL;
    for(index = 0U; index < 4U; index++)
    {
        uint32_t mask = s_button_masks[index];

        if(!(changed & mask))
            continue;
        if(state->ignore_until_released & mask)
        {
            if(!(event->state & mask))
                state->ignore_until_released &= ~mask;
            state->hold_ticks[index] = 0U;
            continue;
        }
        if(event->state & mask)
        {
            if((state->view.state & mask) || !(event->pressed & mask))
                state->view.repeat_error |= mask;
            if(state->view.pressed_count[index] != UINT16_MAX)
                state->view.pressed_count[index]++;
            state->hold_ticks[index] = 0U;
            state->view.passed &= ~mask;
        }
        else
        {
            if(state->view.released_count[index] != UINT16_MAX)
                state->view.released_count[index]++;
            buttons_controller_refresh_passed(state, mask, index);
        }
    }
    state->view.state = event->state & HK_INPUT_BUTTON_ALL;
    state->dirty = 1U;
    if(state->exit_armed &&
       !(state->view.state & (HK_INPUT_BUTTON_OK | HK_INPUT_BUTTON_BACK)))
        state->close_requested = 1U;
}

void buttons_controller_tick(buttons_state_t *state, uint32_t buttons)
{
    uint8_t index;
    uint32_t previous_hold;
    uint8_t previous_armed;

    if(!state)
        return;
    previous_hold = state->view.hold_passed;
    previous_armed = state->exit_armed;
    state->view.state = buttons & HK_INPUT_BUTTON_ALL;
    for(index = 0U; index < 4U; index++)
    {
        uint32_t mask = s_button_masks[index];

        if(!(state->view.state & mask) || (state->ignore_until_released & mask))
        {
            state->hold_ticks[index] = 0U;
            continue;
        }
        if(state->hold_ticks[index] < BUTTONS_HOLD_TICKS)
            state->hold_ticks[index]++;
        if(state->hold_ticks[index] == BUTTONS_HOLD_TICKS &&
           !(state->view.hold_passed & mask))
            state->view.hold_passed |= mask;
    }
    if((state->view.state & (HK_INPUT_BUTTON_OK | HK_INPUT_BUTTON_BACK)) ==
       (HK_INPUT_BUTTON_OK | HK_INPUT_BUTTON_BACK))
    {
        if(state->exit_ticks < BUTTONS_HOLD_TICKS)
            state->exit_ticks++;
        if(state->exit_ticks == BUTTONS_HOLD_TICKS)
            state->exit_armed = 1U;
    }
    else if(!state->exit_armed)
        state->exit_ticks = 0U;
    if(state->exit_armed &&
       !(state->view.state & (HK_INPUT_BUTTON_OK | HK_INPUT_BUTTON_BACK)))
        state->close_requested = 1U;
    if(state->view.hold_passed != previous_hold ||
       state->exit_armed != previous_armed || state->close_requested)
        state->dirty = 1U;
}
