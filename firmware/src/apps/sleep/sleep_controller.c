#include "sleep_controller.h"

#include <stdio.h>
#include <string.h>

#include "sleep_firmware.h"

void sleep_controller_reset(sleep_state_t *state)
{
    if(!state)
        return;
    memset(state, 0, sizeof(*state));
    sleep_session_set_active(1U);
    screen_brightness_off();
    state->dirty = 1U;
    printf("[SHELL] screen SLEEP\r\n");
    printf("[SLEEP] enter\r\n");
}

void sleep_controller_exit(sleep_state_t *state)
{
    if(!state)
        return;
    screen_brightness_apply();
    illum_led_apply();
    rgb_led_apply();
    sleep_session_set_active(0U);
    printf("[SLEEP] wake\r\n");
}

void sleep_controller_handle_input(
    sleep_state_t *state, const hk_input_event_t *event)
{
    if(!state || !event)
        return;
    if(event->pressed)
        state->close_requested = 1U;
}
