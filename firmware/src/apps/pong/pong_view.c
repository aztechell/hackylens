#include "pong_view.h"

#include <stdio.h>

#include "../../drivers/hk_lcd.h"
#include "../../ui/hk_ui.h"

static void pong_fill_clipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if(x < 0)
    {
        w += x;
        x = 0;
    }
    if(y < 0)
    {
        h += y;
        y = 0;
    }
    if(x + w > LCD_W)
        w = LCD_W - x;
    if(y + h > LCD_H)
        h = LCD_H - y;
    if(w <= 0 || h <= 0 || x >= LCD_W || y >= LCD_H)
        return;
    lcd_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
}

static void pong_view_draw_title(pong_view_state_t state)
{
    char title[24];

    snprintf(title, sizeof(title), "YOU  %u : %u  CPU", state.player_score, state.ai_score);
    lcd_fill_rect(MENU_LINE,
                  MENU_LINE,
                  LCD_W - MENU_LINE * 2,
                  MENU_BAR_H - MENU_LINE * 2,
                  COLOR_BLACK);
    lcd_draw_text_centered(3, title, COLOR_TERM_GREEN, COLOR_BLACK);
}

static void pong_view_draw_chrome(void)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COLOR_BLACK);
    lcd_draw_rect(0, 0, LCD_W, LCD_H, MENU_LINE, COLOR_TERM_GREEN);
    lcd_fill_rect(0, MENU_BAR_H - MENU_LINE, LCD_W, MENU_LINE, COLOR_TERM_GREEN);
}

static void pong_view_draw_border(void)
{
    lcd_draw_rect(PONG_FIELD_X, PONG_FIELD_Y, PONG_FIELD_W, PONG_FIELD_H, MENU_LINE, COLOR_TERM_GREEN);
}

static void pong_view_draw_midline(void)
{
    const uint16_t y = PONG_FIELD_Y + PONG_FIELD_H / 2;
    const uint16_t left = PONG_FIELD_X + MENU_LINE;
    const uint16_t right = PONG_FIELD_X + PONG_FIELD_W - MENU_LINE;
    const uint16_t dash = 8;
    const uint16_t step = 16;
    const uint16_t count = (right - left) / step;
    const uint16_t pattern_width = (count - 1) * step + dash;
    const uint16_t start = left + (right - left - pattern_width) / 2;

    for(uint16_t x = start; x + dash <= right; x += step)
        lcd_fill_rect(x, y - 1, dash, 2, COLOR_TERM_GREEN);
}

static void pong_view_draw_trail(pong_view_state_t state, uint16_t color)
{
    for(uint8_t i = 0; i < state.trail_count && i < PONG_TRAIL_LENGTH; i++)
    {
        int16_t size = i == 0 ? PONG_BALL_SIZE - 2 : PONG_BALL_SIZE - 4;
        int16_t inset = (PONG_BALL_SIZE - size) / 2;

        pong_fill_clipped(state.trail_x[i] + inset, state.trail_y[i] + inset, size, size, color);
    }
}

static void pong_view_draw_objects(pong_view_state_t state, uint16_t paddle_color, uint16_t ball_color)
{
    pong_fill_clipped(state.player_x, PONG_PLAYER_Y, PONG_PADDLE_W, PONG_PADDLE_H, paddle_color);
    pong_fill_clipped(state.ai_x, PONG_AI_Y, PONG_PADDLE_W, PONG_PADDLE_H, paddle_color);
    pong_fill_clipped(state.ball_x, state.ball_y, PONG_BALL_SIZE, PONG_BALL_SIZE, ball_color);
}

static void pong_view_draw_flash(pong_view_state_t state, uint16_t color)
{
    if(state.flash_ticks == 0)
        return;
    pong_fill_clipped(state.flash_x - 5, state.flash_y - 1, 11, 3, color);
    pong_fill_clipped(state.flash_x - 1, state.flash_y - 5, 3, 11, color);
}

static void pong_view_draw_dynamic(pong_view_state_t state)
{
    pong_view_draw_trail(state, PONG_TRAIL_COLOR);
    pong_view_draw_objects(state, COLOR_TERM_GREEN, PONG_BALL_COLOR);
    pong_view_draw_flash(state, PONG_FLASH_COLOR);
}

static uint8_t pong_view_trail_changed(pong_view_state_t previous, pong_view_state_t current)
{
    if(previous.trail_count != current.trail_count)
        return 1;

    for(uint8_t i = 0; i < previous.trail_count && i < PONG_TRAIL_LENGTH; i++)
    {
        if(previous.trail_x[i] != current.trail_x[i] || previous.trail_y[i] != current.trail_y[i])
            return 1;
    }
    return 0;
}

static uint8_t pong_view_flash_changed(pong_view_state_t previous, pong_view_state_t current)
{
    uint8_t previous_visible = previous.flash_ticks > 0;
    uint8_t current_visible = current.flash_ticks > 0;

    return previous_visible != current_visible ||
           (previous_visible && (previous.flash_x != current.flash_x || previous.flash_y != current.flash_y));
}

void pong_view_render_initial(pong_view_state_t state)
{
    pong_view_draw_chrome();
    pong_view_draw_border();
    pong_view_draw_midline();
    pong_view_draw_title(state);
    pong_view_draw_dynamic(state);
}

void pong_view_render_score(pong_view_state_t state)
{
    pong_view_draw_title(state);
}

void pong_view_render_frame(pong_view_state_t previous, pong_view_state_t current)
{
    uint8_t player_changed = previous.player_x != current.player_x;
    uint8_t ai_changed = previous.ai_x != current.ai_x;
    uint8_t ball_changed = previous.ball_x != current.ball_x || previous.ball_y != current.ball_y;
    uint8_t trail_changed = pong_view_trail_changed(previous, current);
    uint8_t flash_changed = pong_view_flash_changed(previous, current);

    if(!player_changed && !ai_changed && !ball_changed && !trail_changed && !flash_changed)
        return;

    if(player_changed)
        pong_fill_clipped(previous.player_x, PONG_PLAYER_Y, PONG_PADDLE_W, PONG_PADDLE_H, COLOR_BLACK);
    if(ai_changed)
        pong_fill_clipped(previous.ai_x, PONG_AI_Y, PONG_PADDLE_W, PONG_PADDLE_H, COLOR_BLACK);
    if(ball_changed)
        pong_fill_clipped(previous.ball_x, previous.ball_y, PONG_BALL_SIZE, PONG_BALL_SIZE, COLOR_BLACK);
    if(trail_changed)
        pong_view_draw_trail(previous, COLOR_BLACK);
    if(flash_changed)
        pong_view_draw_flash(previous, COLOR_BLACK);

    pong_view_draw_border();
    pong_view_draw_midline();
    pong_view_draw_dynamic(current);
}

void pong_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)bg;
    lcd_fill_rect(x + 8, y + 10, 4, 40, color);
    lcd_fill_rect(x + 48, y + 10, 4, 40, color);
    lcd_fill_rect(x + 27, y + 27, 6, 6, color);
    lcd_fill_rect(x + 18, y + 16, 2, 2, color);
    lcd_fill_rect(x + 40, y + 42, 2, 2, color);
}
