#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "hk_lcd.h"
#include "input_config.h"
#include "pong_controller.h"
#include "pong_view.h"

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint16_t color;
} fill_call_t;

#define FILL_CALL_MAX 64U

static uint64_t g_now_us;
static fill_call_t g_fill_calls[FILL_CALL_MAX];
static uint16_t g_fill_call_count;
static uint16_t g_draw_rect_count;
static uint16_t g_text_count;
static uint8_t g_record_lcd;

uint64_t time_internal_us(void)
{
    return g_now_us;
}

void hk_screen_set(screen_t screen)
{
    assert(screen == HK_PONG_SCREEN);
}

void hk_back_exit_set_armed(uint8_t armed)
{
    assert(armed == 0U);
}

void shell_show_menu(void)
{
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   uint16_t color)
{
    if(!g_record_lcd)
        return;
    assert(g_fill_call_count < FILL_CALL_MAX);
    g_fill_calls[g_fill_call_count++] = (fill_call_t){x, y, w, h, color};
}

void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   uint16_t thickness, uint16_t color)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)thickness;
    (void)color;
    if(g_record_lcd)
        g_draw_rect_count++;
}

void lcd_draw_text_centered(uint16_t y, const char *text, uint16_t fg,
                            uint16_t bg)
{
    (void)y;
    (void)text;
    (void)fg;
    (void)bg;
    if(g_record_lcd)
        g_text_count++;
}

static void reset_lcd_log(void)
{
    g_fill_call_count = 0U;
    g_draw_rect_count = 0U;
    g_text_count = 0U;
}

static void advance_and_tick(uint32_t elapsed_us,
                             const hk_input_snapshot_t *input)
{
    g_now_us += elapsed_us;
    pong_controller_tick(input);
}

static pong_view_state_t run_regular_schedule(void)
{
    const hk_input_snapshot_t input = {BUTTON_RIGHT, 0U, 0U};

    g_record_lcd = 0U;
    g_now_us = 1000000ULL;
    pong_controller_enter(&input);
    for(uint8_t i = 0; i < 20U; i++)
        advance_and_tick(20000U, &input);
    return pong_controller_test_state();
}

static pong_view_state_t run_irregular_schedule(void)
{
    static const uint32_t pattern[] = {
        7000U, 29000U, 11000U, 53000U, 17000U, 31000U, 19000U, 33000U,
    };
    const hk_input_snapshot_t input = {BUTTON_RIGHT, 0U, 0U};
    uint32_t remaining_us = 400000U;
    uint8_t index = 0U;

    g_record_lcd = 0U;
    g_now_us = 9000000ULL;
    pong_controller_enter(&input);
    while(remaining_us > 0U)
    {
        uint32_t elapsed_us = pattern[index %
            (uint8_t)(sizeof(pattern) / sizeof(pattern[0]))];

        if(elapsed_us > remaining_us)
            elapsed_us = remaining_us;
        advance_and_tick(elapsed_us, &input);
        remaining_us -= elapsed_us;
        index++;
    }
    return pong_controller_test_state();
}

static void assert_state_equal(pong_view_state_t first,
                               pong_view_state_t second)
{
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
    for(uint8_t i = 0; i < PONG_TRAIL_LENGTH; i++)
    {
        assert(first.trail_x[i] == second.trail_x[i]);
        assert(first.trail_y[i] == second.trail_y[i]);
    }
}

static void test_frame_rate_independence(void)
{
    pong_view_state_t regular = run_regular_schedule();
    pong_view_state_t irregular = run_irregular_schedule();
    const int16_t serve_x = PONG_FIELD_X +
        (PONG_FIELD_W - PONG_BALL_SIZE) / 2;

    assert_state_equal(regular, irregular);
    assert(regular.player_x > (LCD_W - PONG_PADDLE_W) / 2);
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
    const uint16_t dirty_x = (uint16_t)previous.ball_x;
    const uint16_t dirty_y = (uint16_t)previous.ball_y;
    const uint16_t dirty_w = PONG_BALL_SIZE + 3U;
    const uint16_t dirty_h = PONG_BALL_SIZE;

    current.ball_x += 3;
    g_record_lcd = 1U;
    reset_lcd_log();
    pong_view_render_frame(previous, current);

    assert(g_draw_rect_count == 0U);
    assert(g_text_count == 0U);
    assert(g_fill_call_count >= 3U);
    for(uint16_t i = 0; i < g_fill_call_count; i++)
    {
        const fill_call_t *call = &g_fill_calls[i];

        assert(call->x >= dirty_x);
        assert(call->y >= dirty_y);
        assert(call->x + call->w <= dirty_x + dirty_w);
        assert(call->y + call->h <= dirty_y + dirty_h);
        assert(call->w < PONG_FIELD_W);
        assert(call->h < PONG_FIELD_H);
        black_calls += call->color == COLOR_BLACK;
        green_calls += call->color == COLOR_TERM_GREEN;
        white_calls += call->color == PONG_BALL_COLOR;
    }
    assert(black_calls > 0U);
    assert(green_calls > 0U);
    assert(white_calls > 0U);

    reset_lcd_log();
    pong_view_render_frame(current, current);
    assert(g_fill_call_count == 0U);
    assert(g_draw_rect_count == 0U);
    assert(g_text_count == 0U);
}

int main(void)
{
    test_frame_rate_independence();
    test_dirty_rendering();
    puts("PONG_HOST_OK fixed_step=20ms dirty_regions=bounded");
    return 0;
}
