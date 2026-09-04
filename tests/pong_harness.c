#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/src/app_runtime/surface_private.h"
#include "../firmware/src/apps/pong/pong_controller.h"
#include "../firmware/src/apps/pong/pong_view.h"

typedef struct
{
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
    uint16_t color;
} fill_call_t;

#define FILL_CALL_MAX 64U
#define K210_BASE_BATCH_COMMANDS 32U

static uint64_t g_now_us;
static fill_call_t g_fill_calls[FILL_CALL_MAX];
static uint16_t g_fill_call_count;
static uint16_t g_clear_count;
static uint16_t g_text_count;
static uint8_t g_record_lcd;
static hk_app_surface_t g_surface;

static hk_result_t stub_invalidate(void *user, const hk_display_rect_t *region)
{
    (void)user;
    (void)region;
    return HK_OK;
}

static hk_result_t record_clear(void *user, uint16_t rgb565)
{
    (void)user;
    (void)rgb565;
    if(g_record_lcd)
        g_clear_count++;
    return HK_OK;
}

static hk_result_t record_fill(
    void *user, const hk_display_rect_t *rect, uint16_t rgb565)
{
    (void)user;
    if(!g_record_lcd)
        return HK_OK;
    assert(rect != NULL);
    assert(g_fill_call_count < FILL_CALL_MAX);
    g_fill_calls[g_fill_call_count++] = (fill_call_t){
        rect->x, rect->y, rect->width, rect->height, rgb565,
    };
    return HK_OK;
}

static hk_result_t record_text(
    void *user,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565)
{
    (void)user;
    (void)bounds;
    (void)utf8;
    (void)size_bytes;
    (void)rgb565;
    if(g_record_lcd)
        g_text_count++;
    return HK_OK;
}

static hk_result_t stub_blit(
    void *user,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    (void)user;
    (void)destination;
    (void)pixels;
    (void)pixel_format;
    return HK_OK;
}

static void init_surface(void)
{
    static const hk_display_info_t info = {
        sizeof(hk_display_info_t), HK_DISPLAY_INFO_VERSION,
        PONG_DISPLAY_WIDTH, PONG_DISPLAY_HEIGHT,
        HK_DISPLAY_FORMAT_RGB565_BE, HK_DISPLAY_PLANE_BASE,
        2U, 2U, 64U, 128U, HK_APP_MAX_INVALIDATIONS, 1U, 64U, 500000U, 0U,
    };
    const hk_app_surface_ops_t ops = {
        .user = NULL,
        .invalidate = stub_invalidate,
        .clear = record_clear,
        .fill_rect = record_fill,
        .stroke_rect = record_fill,
        .text = record_text,
        .blit = stub_blit,
    };

    assert(hk_app_surface_private_init(&g_surface, 1U, &info, &ops) == HK_OK);
}

static void reset_lcd_log(void)
{
    g_fill_call_count = 0U;
    g_clear_count = 0U;
    g_text_count = 0U;
}

static uint16_t recorded_commands(void)
{
    return (uint16_t)(g_clear_count + g_fill_call_count + g_text_count);
}

static void advance_and_tick(pong_state_t *state, uint32_t elapsed_us, uint32_t buttons)
{
    g_now_us += elapsed_us;
    pong_controller_tick(state, buttons, g_now_us);
}

static pong_view_state_t run_regular_schedule(void)
{
    pong_state_t state;

    memset(&state, 0, sizeof(state));
    g_now_us = 1000000ULL;
    pong_controller_reset(&state, g_now_us);
    for(uint8_t i = 0U; i < 20U; i++)
        advance_and_tick(&state, 20000U, HK_INPUT_BUTTON_RIGHT);
    return pong_controller_view_state(&state);
}

static pong_view_state_t run_irregular_schedule(void)
{
    static const uint32_t pattern[] = {
        7000U, 29000U, 11000U, 53000U, 17000U, 31000U, 19000U, 33000U,
    };
    pong_state_t state;
    uint32_t remaining_us = 400000U;
    uint8_t index = 0U;

    memset(&state, 0, sizeof(state));
    g_now_us = 9000000ULL;
    pong_controller_reset(&state, g_now_us);
    while(remaining_us > 0U)
    {
        uint32_t elapsed_us = pattern[index %
            (uint8_t)(sizeof(pattern) / sizeof(pattern[0]))];

        if(elapsed_us > remaining_us)
            elapsed_us = remaining_us;
        advance_and_tick(&state, elapsed_us, HK_INPUT_BUTTON_RIGHT);
        remaining_us -= elapsed_us;
        index++;
    }
    return pong_controller_view_state(&state);
}

static void assert_state_equal(pong_view_state_t first, pong_view_state_t second)
{
    uint8_t index;

    assert(first.player_x == second.player_x);
    assert(first.ai_x == second.ai_x);
    assert(first.ball_x == second.ball_x);
    assert(first.ball_y == second.ball_y);
    assert(first.flash_x == second.flash_x);
    assert(first.flash_y == second.flash_y);
    assert(first.player_score == second.player_score);
    assert(first.ai_score == second.ai_score);
    assert(first.trail_count == second.trail_count);
    assert(first.flash_ticks == second.flash_ticks);
    for(index = 0U; index < PONG_TRAIL_LENGTH; index++)
    {
        assert(first.trail_x[index] == second.trail_x[index]);
        assert(first.trail_y[index] == second.trail_y[index]);
    }
}

static void test_frame_rate_independence(void)
{
    pong_view_state_t regular = run_regular_schedule();
    pong_view_state_t irregular = run_irregular_schedule();
    const int16_t serve_x = PONG_FIELD_X +
        (PONG_FIELD_W - PONG_BALL_SIZE) / 2;

    assert_state_equal(regular, irregular);
    assert(regular.player_x >
           (PONG_DISPLAY_WIDTH - PONG_PADDLE_W) / 2);
    assert(regular.ball_x != serve_x);
}

static void test_dirty_rendering(void)
{
    pong_view_state_t previous = {
        .player_x = 100,
        .ai_x = 120,
        .ball_x = 78,
        .ball_y = PONG_FIELD_Y + PONG_FIELD_H / 2 - 3,
    };
    pong_view_state_t current = previous;
    uint16_t black_calls = 0U;
    uint16_t green_calls = 0U;
    uint16_t white_calls = 0U;
    const int32_t dirty_x = previous.ball_x;
    const int32_t dirty_y = previous.ball_y;
    const uint32_t dirty_w = (uint32_t)PONG_BALL_SIZE + 3U;
    const uint32_t dirty_h = (uint32_t)PONG_BALL_SIZE;
    uint8_t full = 0U;
    hk_display_rect_t regions[HK_APP_MAX_INVALIDATIONS];
    uint8_t count;

    current.ball_x += 3;
    g_record_lcd = 1U;
    reset_lcd_log();
    assert(pong_view_render_frame(&g_surface, previous, current) == HK_OK);

    count = pong_view_collect_invalidations(
        previous, current, 0U, regions, HK_APP_MAX_INVALIDATIONS, &full);
    assert(full == 0U);
    assert(count > 0U && count <= HK_APP_MAX_INVALIDATIONS);
    assert(g_clear_count == 0U);
    assert(g_text_count == 0U);
    assert(g_fill_call_count >= 3U);
    for(uint16_t i = 0U; i < g_fill_call_count; i++)
    {
        const fill_call_t *call = &g_fill_calls[i];

        assert(call->x >= dirty_x);
        assert(call->y >= dirty_y);
        assert(call->x + (int32_t)call->w <= dirty_x + (int32_t)dirty_w);
        assert(call->y + (int32_t)call->h <= dirty_y + (int32_t)dirty_h);
        assert(call->w < (uint32_t)PONG_FIELD_W);
        assert(call->h < (uint32_t)PONG_FIELD_H);
        black_calls += call->color == PONG_COLOR_BLACK;
        green_calls += call->color == PONG_COLOR_GREEN;
        white_calls += call->color == PONG_BALL_COLOR;
    }
    assert(black_calls > 0U);
    assert(green_calls > 0U);
    assert(white_calls > 0U);

    reset_lcd_log();
    assert(pong_view_render_frame(&g_surface, current, current) == HK_OK);
    assert(g_fill_call_count == 0U);
    assert(g_clear_count == 0U);
    assert(g_text_count == 0U);
}

static void test_single_present_for_full_and_maximal_frames(void)
{
    pong_view_state_t previous = {
        .player_x = 12,
        .ai_x = 250,
        .ball_x = 40,
        .ball_y = 90,
        .trail_x = {70, 100},
        .trail_y = {120, 150},
        .flash_x = 180,
        .flash_y = 110,
        .trail_count = PONG_TRAIL_LENGTH,
        .flash_ticks = 1U,
    };
    pong_view_state_t current = {
        .player_x = 210,
        .ai_x = 30,
        .ball_x = 270,
        .ball_y = 180,
        .trail_x = {220, 250},
        .trail_y = {70, 40},
        .flash_x = 140,
        .flash_y = 170,
        .trail_count = PONG_TRAIL_LENGTH,
        .flash_ticks = 1U,
    };
    uint8_t full = 0U;
    hk_display_rect_t regions[HK_APP_MAX_INVALIDATIONS];
    uint8_t count;

    g_record_lcd = 1U;
    reset_lcd_log();
    assert(pong_view_render_initial(&g_surface, current) == HK_OK);
    assert(g_clear_count == 1U);
    assert(g_text_count == 1U);
    assert(recorded_commands() <= K210_BASE_BATCH_COMMANDS);

    reset_lcd_log();
    assert(pong_view_render_score(&g_surface, current) == HK_OK);
    assert(g_clear_count == 0U);
    assert(g_text_count == 1U);
    assert(g_fill_call_count >= 1U);
    assert(g_fill_calls[0].h < (uint32_t)PONG_FIELD_H);

    reset_lcd_log();
    assert(pong_view_render_frame(&g_surface, previous, current) == HK_OK);
    count = pong_view_collect_invalidations(
        previous, current, 0U, regions, HK_APP_MAX_INVALIDATIONS, &full);
    assert(g_clear_count == 0U);
    assert(full == 0U);
    assert(count > 0U && count <= HK_APP_MAX_INVALIDATIONS);
    assert(recorded_commands() <= K210_BASE_BATCH_COMMANDS);
}

int main(void)
{
    init_surface();
    test_frame_rate_independence();
    test_dirty_rendering();
    test_single_present_for_full_and_maximal_frames();
    puts("PONG_HOST_OK fixed_step=20ms dirty_regions=bounded presents=1");
    return 0;
}
