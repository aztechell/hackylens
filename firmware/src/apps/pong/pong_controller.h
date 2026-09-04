#ifndef PONG_CONTROLLER_H
#define PONG_CONTROLLER_H

#include <hackylens/app.h>

#include "pong_view.h"

typedef struct
{
    pong_view_state_t previous;
    hk_owner_t owner;
    hk_time_t time;
    hk_input_t input;
    int16_t player_x;
    int16_t ai_x;
    int16_t ai_target_x;
    int16_t ball_x;
    int16_t ball_y;
    int16_t ball_dx;
    int16_t ball_dy;
    int16_t player_dx;
    int16_t ai_dx;
    int16_t trail_x[PONG_TRAIL_LENGTH];
    int16_t trail_y[PONG_TRAIL_LENGTH];
    int16_t flash_x;
    int16_t flash_y;
    uint64_t last_tick_us;
    uint64_t accumulator_us;
    uint32_t buttons;
    uint8_t player_score;
    uint8_t ai_score;
    uint8_t rally_hits;
    uint8_t trail_count;
    uint8_t flash_ticks;
    uint8_t serve_ticks;
    uint8_t serve_index;
    uint8_t ai_reaction_ticks;
    uint8_t ai_noise;
    int8_t serve_direction;
    uint8_t close_requested;
    uint8_t need_full_redraw;
    uint8_t score_changed;
    uint8_t dirty;
} pong_state_t;

void pong_controller_reset(pong_state_t *state, uint64_t now_us);
void pong_controller_handle_input(
    pong_state_t *state, const hk_input_event_t *event);
void pong_controller_tick(
    pong_state_t *state, uint32_t buttons, uint64_t now_us);
pong_view_state_t pong_controller_view_state(const pong_state_t *state);

#endif
