#ifndef PONG_VIEW_H
#define PONG_VIEW_H

#include <stdint.h>

#include <hackylens/app.h>

#include "pong_config.h"

typedef struct
{
    int16_t player_x;
    int16_t ai_x;
    int16_t ball_x;
    int16_t ball_y;
    int16_t trail_x[PONG_TRAIL_LENGTH];
    int16_t trail_y[PONG_TRAIL_LENGTH];
    int16_t flash_x;
    int16_t flash_y;
    uint8_t player_score;
    uint8_t ai_score;
    uint8_t trail_count;
    uint8_t flash_ticks;
} pong_view_state_t;

hk_result_t pong_view_render_initial(
    hk_app_surface_t *surface, pong_view_state_t state);
hk_result_t pong_view_render_score(
    hk_app_surface_t *surface, pong_view_state_t state);
hk_result_t pong_view_render_frame(
    hk_app_surface_t *surface,
    pong_view_state_t previous,
    pong_view_state_t current);
uint8_t pong_view_collect_invalidations(
    pong_view_state_t previous,
    pong_view_state_t current,
    uint8_t score_changed,
    hk_display_rect_t *regions,
    uint8_t max_regions,
    uint8_t *full);

#endif
