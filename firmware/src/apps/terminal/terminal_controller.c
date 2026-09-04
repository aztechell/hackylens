#include "terminal_controller.h"

#include <stdio.h>
#include <string.h>

#include "terminal_buffer.h"
#include "terminal_config.h"
#include "terminal_firmware.h"
#include "terminal_view.h"

static uint8_t s_buffer_initialized;
static terminal_state_t *s_active;

static void terminal_log_sink(const char *message)
{
    terminal_buffer_write(message);
    if(s_active)
        s_active->dirty = 1U;
}

static void terminal_repeat_reset(terminal_state_t *state)
{
    state->repeat_button = 0U;
    state->repeat_ticks = 0U;
}

static void terminal_repeat_start(terminal_state_t *state, uint32_t button)
{
    state->repeat_button = button;
    state->repeat_ticks = TERMINAL_REPEAT_INITIAL_TICKS;
}

static void terminal_scroll(terminal_state_t *state, uint32_t button)
{
    if(button == HK_INPUT_BUTTON_LEFT)
        terminal_buffer_scroll_up();
    else if(button == HK_INPUT_BUTTON_RIGHT)
        terminal_buffer_scroll_down();
    state->dirty = 1U;
}

static void terminal_toggle_font(terminal_state_t *state)
{
    terminal_geometry_t geometry;
    uint8_t flags = settings_feature_flags();

    state->font_size = state->font_size == TERMINAL_FONT_NORMAL ?
        TERMINAL_FONT_SMALL : TERMINAL_FONT_NORMAL;
    geometry = terminal_view_geometry(state->font_size);
    terminal_buffer_set_geometry(geometry.columns, geometry.rows);
    if(state->font_size == TERMINAL_FONT_SMALL)
        flags |= TERMINAL_SETTINGS_FONT_SMALL_FLAG;
    else
        flags &= (uint8_t)~TERMINAL_SETTINGS_FONT_SMALL_FLAG;
    settings_set_feature_flags(flags);
    settings_mark_dirty(1U);
    state->dirty = 1U;
    shell_printf(
        "[TERM] font %s\r\n",
        state->font_size == TERMINAL_FONT_SMALL ? "SMALL" : "NORMAL");
}

void terminal_controller_reset(terminal_state_t *state)
{
    terminal_geometry_t geometry;
    uint8_t first_init = 0U;

    if(!state)
        return;
    memset(state, 0, sizeof(*state));
    state->font_size = (settings_feature_flags() &
                        TERMINAL_SETTINGS_FONT_SMALL_FLAG) ?
        TERMINAL_FONT_SMALL : TERMINAL_FONT_NORMAL;
    geometry = terminal_view_geometry(state->font_size);
    if(!s_buffer_initialized)
    {
        terminal_buffer_init(geometry.columns, geometry.rows);
        s_buffer_initialized = 1U;
        first_init = 1U;
    }
    else
        terminal_buffer_set_geometry(geometry.columns, geometry.rows);

    s_active = state;
    shell_log_set_sink(terminal_log_sink);
    state->dirty = 1U;
    if(first_init)
    {
        shell_printf("[BOOT] HackyLens LCD terminal\r\n");
        shell_printf("[BOOT] HackyLens %s full\r\n", HACKYLENS_VERSION);
        shell_printf("[DISPLAY] capability minimum %ux%u\r\n",
                     (unsigned)HK_DISPLAY_REQUIRED_WIDTH,
                     (unsigned)HK_DISPLAY_REQUIRED_HEIGHT);
        shell_printf("[TERM] LEFT/RIGHT scroll, OK latest\r\n");
        shell_printf("[TERM] hold OK toggles font, BACK menu\r\n");
    }
    else
        shell_printf("[TERM] opened\r\n");
}

void terminal_controller_exit(terminal_state_t *state)
{
    (void)state;
    shell_log_set_sink(NULL);
    s_active = NULL;
}

void terminal_controller_tick(terminal_state_t *state, uint32_t buttons)
{
    if(!state)
        return;
    if(state->repeat_button != 0U)
    {
        if(!(buttons & state->repeat_button))
            terminal_repeat_reset(state);
        else if(state->repeat_ticks > 0U)
            state->repeat_ticks--;
        else
        {
            terminal_scroll(state, state->repeat_button);
            state->repeat_ticks = TERMINAL_REPEAT_NEXT_TICKS;
        }
    }

    if(state->ok_active && (buttons & HK_INPUT_BUTTON_OK) &&
       !state->ok_hold_fired)
    {
        if(state->ok_hold_ticks < TERMINAL_OK_HOLD_TICKS)
            state->ok_hold_ticks++;
        if(state->ok_hold_ticks >= TERMINAL_OK_HOLD_TICKS)
        {
            state->ok_hold_fired = 1U;
            terminal_toggle_font(state);
        }
    }
}

void terminal_controller_handle_input(
    terminal_state_t *state, const hk_input_event_t *event)
{
    if(!state || !event)
        return;
    if(event->pressed & HK_INPUT_BUTTON_BACK)
    {
        state->close_requested = 1U;
        return;
    }
    if(event->pressed & HK_INPUT_BUTTON_LEFT)
    {
        terminal_scroll(state, HK_INPUT_BUTTON_LEFT);
        terminal_repeat_start(state, HK_INPUT_BUTTON_LEFT);
    }
    if(event->pressed & HK_INPUT_BUTTON_RIGHT)
    {
        terminal_scroll(state, HK_INPUT_BUTTON_RIGHT);
        terminal_repeat_start(state, HK_INPUT_BUTTON_RIGHT);
    }
    if(event->pressed & HK_INPUT_BUTTON_OK)
    {
        state->ok_active = 1U;
        state->ok_hold_ticks = 0U;
        state->ok_hold_fired = 0U;
    }
    if((event->changed & HK_INPUT_BUTTON_OK) &&
       !(event->state & HK_INPUT_BUTTON_OK) && state->ok_active)
    {
        if(!state->ok_hold_fired)
        {
            terminal_buffer_follow_latest();
            state->dirty = 1U;
        }
        state->ok_active = 0U;
        state->ok_hold_ticks = 0U;
        state->ok_hold_fired = 0U;
    }
}
