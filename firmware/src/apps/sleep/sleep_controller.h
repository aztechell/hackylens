#ifndef SLEEP_CONTROLLER_H
#define SLEEP_CONTROLLER_H

#include <hackylens/app.h>

typedef struct
{
    hk_owner_t owner;
    hk_input_t input;
    uint8_t close_requested;
    uint8_t dirty;
} sleep_state_t;

void sleep_controller_reset(sleep_state_t *state);
void sleep_controller_exit(sleep_state_t *state);
void sleep_controller_handle_input(
    sleep_state_t *state, const hk_input_event_t *event);

#endif
