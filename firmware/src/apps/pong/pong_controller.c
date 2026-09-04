#include "pong_controller.h"

#include <stdio.h>
#include <string.h>

#include "pong_config.h"

pong_view_state_t pong_controller_view_state(const pong_state_t *state)
{
    pong_view_state_t view = {0};
    uint8_t index;

    if(!state)
        return view;
    view.player_x = state->player_x;
    view.ai_x = state->ai_x;
    view.ball_x = state->ball_x;
    view.ball_y = state->ball_y;
    view.flash_x = state->flash_x;
    view.flash_y = state->flash_y;
    view.player_score = state->player_score;
    view.ai_score = state->ai_score;
    view.trail_count = state->trail_count;
    view.flash_ticks = state->flash_ticks;
    for(index = 0U; index < PONG_TRAIL_LENGTH; index++)
    {
        view.trail_x[index] = state->trail_x[index];
        view.trail_y[index] = state->trail_y[index];
    }
    return view;
}

static int16_t pong_clamp(int16_t value, int16_t minimum, int16_t maximum)
{
    if(value < minimum)
        return minimum;
    if(value > maximum)
        return maximum;
    return value;
}

static int16_t pong_paddle_min_x(void)
{
    return PONG_FIELD_X + PONG_MENU_LINE;
}

static int16_t pong_paddle_max_x(void)
{
    return PONG_FIELD_X + PONG_FIELD_W - PONG_MENU_LINE - PONG_PADDLE_W;
}

static int16_t pong_ball_min_x(void)
{
    return PONG_FIELD_X + PONG_MENU_LINE;
}

static int16_t pong_ball_max_x(void)
{
    return PONG_FIELD_X + PONG_FIELD_W - PONG_MENU_LINE - PONG_BALL_SIZE;
}

static void pong_clear_effects(pong_state_t *state)
{
    uint8_t index;

    state->trail_count = 0U;
    state->flash_ticks = 0U;
    for(index = 0U; index < PONG_TRAIL_LENGTH; index++)
    {
        state->trail_x[index] = state->ball_x;
        state->trail_y[index] = state->ball_y;
    }
}

static void pong_begin_serve(pong_state_t *state, int8_t direction)
{
    state->ball_x = PONG_FIELD_X + (PONG_FIELD_W - PONG_BALL_SIZE) / 2;
    state->ball_y = PONG_FIELD_Y + (PONG_FIELD_H - PONG_BALL_SIZE) / 2;
    state->ball_dx = 0;
    state->ball_dy = 0;
    state->serve_direction = direction < 0 ? -1 : 1;
    state->serve_ticks = PONG_SERVE_DELAY_TICKS;
    state->rally_hits = 0U;
    state->ai_target_x = (PONG_DISPLAY_WIDTH - PONG_PADDLE_W) / 2;
    state->ai_reaction_ticks = PONG_AI_REACTION_TICKS;
    pong_clear_effects(state);
}

static void pong_launch_serve(pong_state_t *state)
{
    static const int8_t serve_dx[] = {
        -PONG_BALL_START_DX,
        PONG_BALL_START_DX,
        -(PONG_BALL_START_DX - 1),
        PONG_BALL_START_DX - 1,
    };

    state->ball_dx = serve_dx[state->serve_index %
        (uint8_t)(sizeof(serve_dx) / sizeof(serve_dx[0]))];
    state->ball_dy = state->serve_direction * PONG_BALL_START_DY;
    state->serve_index++;
}

static uint8_t pong_intersects(int16_t bx, int16_t by, int16_t px, int16_t py)
{
    return bx + PONG_BALL_SIZE >= px && bx <= px + PONG_PADDLE_W &&
           by + PONG_BALL_SIZE >= py && by <= py + PONG_PADDLE_H;
}

static uint8_t pong_next_noise(pong_state_t *state)
{
    uint8_t value = state->ai_noise;

    value ^= (uint8_t)(value << 3);
    value ^= (uint8_t)(value >> 5);
    value ^= (uint8_t)(value << 1);
    if(value == 0U)
        value = 0xA5;
    state->ai_noise = value;
    return value;
}

static int16_t pong_reflect_ball_x(int32_t projected_x)
{
    int32_t left = pong_ball_min_x();
    int32_t span = pong_ball_max_x() - left;
    int32_t period = span * 2;
    int32_t relative = (projected_x - left) % period;

    if(relative < 0)
        relative += period;
    if(relative > span)
        relative = period - relative;
    return (int16_t)(left + relative);
}

static void pong_update_ai_target(pong_state_t *state)
{
    int16_t target = (PONG_DISPLAY_WIDTH - PONG_PADDLE_W) / 2;

    if(state->ball_dy < 0)
    {
        int16_t distance = state->ball_y - (PONG_AI_Y + PONG_PADDLE_H);
        int16_t vertical_speed = -state->ball_dy;
        int16_t travel_ticks;
        int32_t projected_x;
        int16_t error;

        if(distance < 0)
            distance = 0;
        travel_ticks = (distance + vertical_speed - 1) / vertical_speed;
        projected_x = state->ball_x + (int32_t)state->ball_dx * travel_ticks;
        error = (int16_t)(pong_next_noise(state) % (PONG_AI_ERROR_PX * 2 + 1)) -
            PONG_AI_ERROR_PX;
        target = pong_reflect_ball_x(projected_x) + PONG_BALL_SIZE / 2 -
            PONG_PADDLE_W / 2 + error;
    }
    state->ai_target_x = pong_clamp(target, pong_paddle_min_x(), pong_paddle_max_x());
}

static void pong_update_player(pong_state_t *state, uint32_t buttons)
{
    int16_t previous_x = state->player_x;

    if(buttons & HK_INPUT_BUTTON_LEFT)
        state->player_x -= PONG_PADDLE_SPEED;
    if(buttons & HK_INPUT_BUTTON_RIGHT)
        state->player_x += PONG_PADDLE_SPEED;
    state->player_x = pong_clamp(
        state->player_x, pong_paddle_min_x(), pong_paddle_max_x());
    state->player_dx = state->player_x - previous_x;
}

static void pong_update_ai(pong_state_t *state)
{
    int16_t previous_x = state->ai_x;
    int16_t distance;

    if(state->ai_reaction_ticks > 0U)
        state->ai_reaction_ticks--;
    else
    {
        pong_update_ai_target(state);
        state->ai_reaction_ticks = PONG_AI_REACTION_TICKS - 1;
    }

    distance = state->ai_target_x - state->ai_x;
    if(distance < -PONG_AI_DEAD_ZONE)
        state->ai_x -= distance < -PONG_AI_SPEED ? PONG_AI_SPEED : -distance;
    else if(distance > PONG_AI_DEAD_ZONE)
        state->ai_x += distance > PONG_AI_SPEED ? PONG_AI_SPEED : distance;
    state->ai_x = pong_clamp(state->ai_x, pong_paddle_min_x(), pong_paddle_max_x());
    state->ai_dx = state->ai_x - previous_x;
}

static void pong_push_trail(pong_state_t *state)
{
    uint8_t index;

    for(index = PONG_TRAIL_LENGTH - 1U; index > 0U; index--)
    {
        state->trail_x[index] = state->trail_x[index - 1U];
        state->trail_y[index] = state->trail_y[index - 1U];
    }
    state->trail_x[0] = state->ball_x;
    state->trail_y[0] = state->ball_y;
    if(state->trail_count < PONG_TRAIL_LENGTH)
        state->trail_count++;
}

static int16_t pong_rally_vertical_speed(const pong_state_t *state)
{
    int16_t speed = PONG_BALL_START_DY + state->rally_hits / PONG_RALLY_SPEEDUP_EVERY;

    return speed > PONG_BALL_MAX_DY ? PONG_BALL_MAX_DY : speed;
}

static int16_t pong_bounce_horizontal(
    pong_state_t *state, int16_t paddle_x, int16_t paddle_dx)
{
    int16_t ball_center = state->ball_x + PONG_BALL_SIZE / 2;
    int16_t paddle_center = paddle_x + PONG_PADDLE_W / 2;
    int16_t offset = ball_center - paddle_center;
    int16_t result = (int16_t)(((int32_t)offset * PONG_BALL_MAX_DX) /
        (PONG_PADDLE_W / 2));

    result += paddle_dx / 2;
    result = pong_clamp(result, -PONG_BALL_MAX_DX, PONG_BALL_MAX_DX);
    if(result == 0)
        result = state->ball_dx < 0 ? -1 : 1;
    return result;
}

static void pong_flash_hit(pong_state_t *state, int16_t paddle_y)
{
    state->flash_x = state->ball_x + PONG_BALL_SIZE / 2;
    state->flash_y = paddle_y + PONG_PADDLE_H / 2;
    state->flash_ticks = PONG_FLASH_TICKS;
}

static void pong_advance_ball(pong_state_t *state)
{
    pong_push_trail(state);
    state->ball_x += state->ball_dx;
    state->ball_y += state->ball_dy;

    if(state->ball_x <= pong_ball_min_x())
    {
        state->ball_x = pong_ball_min_x();
        if(state->ball_dx < 0)
            state->ball_dx = -state->ball_dx;
    }
    else if(state->ball_x >= pong_ball_max_x())
    {
        state->ball_x = pong_ball_max_x();
        if(state->ball_dx > 0)
            state->ball_dx = -state->ball_dx;
    }

    if(state->ball_dy > 0 &&
       pong_intersects(state->ball_x, state->ball_y, state->player_x, PONG_PLAYER_Y))
    {
        state->ball_y = PONG_PLAYER_Y - PONG_BALL_SIZE;
        if(state->rally_hits < UINT8_MAX)
            state->rally_hits++;
        state->ball_dx = pong_bounce_horizontal(
            state, state->player_x, state->player_dx);
        state->ball_dy = -pong_rally_vertical_speed(state);
        pong_flash_hit(state, PONG_PLAYER_Y);
    }
    else if(state->ball_dy < 0 &&
            pong_intersects(state->ball_x, state->ball_y, state->ai_x, PONG_AI_Y))
    {
        state->ball_y = PONG_AI_Y + PONG_PADDLE_H;
        if(state->rally_hits < UINT8_MAX)
            state->rally_hits++;
        state->ball_dx = pong_bounce_horizontal(
            state, state->ai_x, state->ai_dx);
        state->ball_dy = pong_rally_vertical_speed(state);
        pong_flash_hit(state, PONG_AI_Y);
    }
}

static uint8_t pong_update_score(pong_state_t *state)
{
    if(state->ball_y + PONG_BALL_SIZE < PONG_FIELD_Y + PONG_MENU_LINE)
    {
        if(state->player_score < 99U)
            state->player_score++;
        pong_begin_serve(state, -1);
        return 1U;
    }
    if(state->ball_y > PONG_FIELD_Y + PONG_FIELD_H - PONG_MENU_LINE)
    {
        if(state->ai_score < 99U)
            state->ai_score++;
        pong_begin_serve(state, 1);
        return 1U;
    }
    return 0U;
}

static uint8_t pong_simulate_step(pong_state_t *state, uint32_t buttons)
{
    uint8_t ball_was_active = state->serve_ticks == 0U;

    if(state->flash_ticks > 0U)
        state->flash_ticks--;
    pong_update_player(state, buttons);

    if(state->serve_ticks > 0U)
    {
        state->serve_ticks--;
        if(state->serve_ticks == 0U)
            pong_launch_serve(state);
    }
    pong_update_ai(state);

    if(!ball_was_active)
        return 0U;
    pong_advance_ball(state);
    return pong_update_score(state);
}

void pong_controller_reset(pong_state_t *state, uint64_t now_us)
{
    hk_owner_t owner;
    hk_time_t time;
    hk_input_t input;
    uint32_t buttons;

    if(!state)
        return;
    owner = state->owner;
    time = state->time;
    input = state->input;
    buttons = state->buttons;
    memset(state, 0, sizeof(*state));
    state->owner = owner;
    state->time = time;
    state->input = input;
    state->buttons = buttons;
    state->player_x = (PONG_DISPLAY_WIDTH - PONG_PADDLE_W) / 2;
    state->ai_x = (PONG_DISPLAY_WIDTH - PONG_PADDLE_W) / 2;
    state->ai_noise = 0xA5;
    state->last_tick_us = now_us;
    pong_begin_serve(state, 1);
    state->previous = pong_controller_view_state(state);
    state->need_full_redraw = 1U;
    state->dirty = 1U;
}

void pong_controller_handle_input(
    pong_state_t *state, const hk_input_event_t *event)
{
    if(!state || !event)
        return;
    if(event->pressed & HK_INPUT_BUTTON_BACK)
    {
        state->close_requested = 1U;
        return;
    }
    if(event->pressed & HK_INPUT_BUTTON_OK)
    {
        pong_controller_reset(state, state->last_tick_us);
        printf("[PONG] reset\r\n");
    }
}

void pong_controller_tick(
    pong_state_t *state, uint32_t buttons, uint64_t now_us)
{
    const uint64_t maximum_accumulator =
        PONG_FIXED_STEP_US * PONG_MAX_CATCH_UP_STEPS;
    uint64_t elapsed_us;
    uint8_t score_changed = 0U;
    uint8_t steps = 0U;

    if(!state)
        return;
    state->buttons = buttons;
    if(now_us < state->last_tick_us)
    {
        state->last_tick_us = now_us;
        state->accumulator_us = 0U;
        return;
    }

    elapsed_us = now_us - state->last_tick_us;
    state->last_tick_us = now_us;
    if(elapsed_us >= maximum_accumulator ||
       state->accumulator_us >= maximum_accumulator - elapsed_us)
        state->accumulator_us = maximum_accumulator;
    else
        state->accumulator_us += elapsed_us;

    if(state->accumulator_us < PONG_FIXED_STEP_US)
        return;

    state->previous = pong_controller_view_state(state);
    while(state->accumulator_us >= PONG_FIXED_STEP_US &&
          steps < PONG_MAX_CATCH_UP_STEPS)
    {
        score_changed |= pong_simulate_step(state, buttons);
        state->accumulator_us -= PONG_FIXED_STEP_US;
        steps++;
    }
    state->score_changed = (uint8_t)(state->score_changed | score_changed);
    state->dirty = 1U;
}
