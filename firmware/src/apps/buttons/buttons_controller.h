#ifndef BUTTONS_CONTROLLER_H
#define BUTTONS_CONTROLLER_H

#include <stdint.h>

#include <hackylens/app.h>

#define BUTTONS_HOLD_TICKS 50U

typedef struct
{
    uint32_t state;
    uint32_t hold_passed;
    uint32_t passed;
    uint32_t repeat_error;
    uint16_t pressed_count[4];
    uint16_t released_count[4];
} buttons_view_state_t;

typedef struct
{
    buttons_view_state_t view;
    hk_owner_t owner;
    hk_input_t input;
    uint32_t ignore_until_released;
    uint8_t hold_ticks[4];
    uint8_t exit_ticks;
    uint8_t exit_armed;
    uint8_t close_requested;
    uint8_t dirty;
} buttons_state_t;

void buttons_controller_reset(buttons_state_t *state, uint32_t initial_buttons);
void buttons_controller_handle_input(
    buttons_state_t *state, const hk_input_event_t *event);
void buttons_controller_tick(buttons_state_t *state, uint32_t buttons);

#endif
