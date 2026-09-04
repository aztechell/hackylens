#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/src/apps/buttons/buttons_controller.h"

static unsigned g_failures;

static void check(int condition, const char *message)
{
    if(!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

static hk_input_event_t make_event(
    uint32_t state, uint32_t pressed, uint32_t changed)
{
    hk_input_event_t event = {0};

    event.state = state;
    event.pressed = pressed;
    event.changed = changed;
    event.released = changed & ~pressed;
    return event;
}

static void complete_button(buttons_state_t *state, uint32_t mask, uint8_t index)
{
    hk_input_event_t event = make_event(mask, mask, mask);
    unsigned tick;

    buttons_controller_handle_input(state, &event);
    for(tick = 0U; tick < BUTTONS_HOLD_TICKS; tick++)
        buttons_controller_tick(state, mask);
    check((state->view.hold_passed & mask) != 0U, "hold must be observed");
    event = make_event(0U, 0U, mask);
    buttons_controller_handle_input(state, &event);
    check(state->view.pressed_count[index] == 1U, "press count must be one");
    check(state->view.released_count[index] == 1U, "release count must be one");
    check((state->view.passed & mask) != 0U, "completed button must pass");
}

int main(void)
{
    buttons_state_t state;
    hk_input_event_t event;
    unsigned tick;

    memset(&state, 0, sizeof(state));
    buttons_controller_reset(&state, HK_INPUT_BUTTON_OK);
    event = make_event(0U, 0U, HK_INPUT_BUTTON_OK);
    buttons_controller_handle_input(&state, &event);
    check(state.view.pressed_count[1] == 0U, "menu OK press must be ignored");
    check(state.view.released_count[1] == 0U, "menu OK release must be ignored");

    complete_button(&state, HK_INPUT_BUTTON_LEFT, 0U);
    complete_button(&state, HK_INPUT_BUTTON_OK, 1U);
    complete_button(&state, HK_INPUT_BUTTON_RIGHT, 2U);
    complete_button(&state, HK_INPUT_BUTTON_BACK, 3U);
    check(state.view.passed == HK_INPUT_BUTTON_ALL, "all four buttons must pass");
    check(state.close_requested == 0U, "plain BACK must not exit the test");

    event = make_event(HK_INPUT_BUTTON_OK, HK_INPUT_BUTTON_OK, HK_INPUT_BUTTON_OK);
    buttons_controller_handle_input(&state, &event);
    event = make_event(
        HK_INPUT_BUTTON_OK | HK_INPUT_BUTTON_BACK,
        HK_INPUT_BUTTON_BACK, HK_INPUT_BUTTON_BACK);
    buttons_controller_handle_input(&state, &event);
    for(tick = 0U; tick < BUTTONS_HOLD_TICKS; tick++)
        buttons_controller_tick(
            &state, HK_INPUT_BUTTON_OK | HK_INPUT_BUTTON_BACK);
    event = make_event(HK_INPUT_BUTTON_BACK, 0U, HK_INPUT_BUTTON_OK);
    buttons_controller_handle_input(&state, &event);
    check(state.close_requested == 0U, "exit waits for both chord buttons to release");
    event = make_event(0U, 0U, HK_INPUT_BUTTON_BACK);
    buttons_controller_handle_input(&state, &event);
    check(state.close_requested == 1U, "held OK+BACK chord must exit on release");

    buttons_controller_reset(&state, 0U);
    event = make_event(
        HK_INPUT_BUTTON_LEFT, HK_INPUT_BUTTON_LEFT, HK_INPUT_BUTTON_LEFT);
    buttons_controller_handle_input(&state, &event);
    buttons_controller_handle_input(&state, &event);
    check(
        (state.view.repeat_error & HK_INPUT_BUTTON_LEFT) != 0U,
        "duplicate press while down must be reported");

    if(g_failures)
        return 1;
    puts("BUTTONS_CONTROLLER_OK press_release=1 hold=1 repeat=1 back=1");
    return 0;
}
