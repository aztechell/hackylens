#ifndef HK_TERMINAL_CONTROLLER_H
#define HK_TERMINAL_CONTROLLER_H

#include <stdint.h>

#include <hackylens/app.h>

#include "terminal_types.h"

typedef struct
{
    hk_owner_t owner;
    hk_input_t input;
    terminal_font_size_t font_size;
    uint32_t repeat_button;
    uint16_t repeat_ticks;
    uint16_t ok_hold_ticks;
    uint8_t ok_active;
    uint8_t ok_hold_fired;
    uint8_t dirty;
    uint8_t close_requested;
} terminal_state_t;

void terminal_controller_reset(terminal_state_t *state);
void terminal_controller_exit(terminal_state_t *state);
void terminal_controller_handle_input(
    terminal_state_t *state, const hk_input_event_t *event);
void terminal_controller_tick(terminal_state_t *state, uint32_t buttons);

#endif
