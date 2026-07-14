#include "settings_controller.h"

#include <stdio.h>

#include "../core/hk_app.h"

#include "../config/input_config.h"
#include "../config/settings_config.h"
#include "../config/settings_layout.h"
#include "settings_actions.h"
#include "settings_model.h"
#include "../core/hk_back_exit.h"
#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "../services/settings_persistence.h"

static uint8_t g_settings_index;
static uint8_t g_settings_top;
static uint8_t g_settings_editing;
static uint32_t g_settings_repeat_button;
static uint8_t g_settings_repeat_ticks;

static void settings_ensure_visible(void)
{
    settings_model_ensure_visible(g_settings_index, &g_settings_top);
}

static void settings_draw_row(uint8_t index)
{
    settings_model_draw_row(g_settings_index, g_settings_top, g_settings_editing, index);
}

static void settings_screen_render(void)
{
    settings_ensure_visible();
    settings_model_render(g_settings_index, g_settings_top, g_settings_editing);
}

void settings_controller_enter(const hk_input_snapshot_t *input)
{
    hk_screen_set(SCREEN_SETTINGS);
    hk_back_exit_set_armed(0);
    g_settings_editing = 0;
    settings_ensure_visible();
    settings_screen_render();
    printf("[SHELL] screen SETTINGS\r\n");
}

static void settings_select_delta(int8_t delta)
{
    uint8_t previous = g_settings_index;
    uint8_t previous_top = g_settings_top;

    if(delta < 0)
        g_settings_index = g_settings_index == 0 ? SETTINGS_ROW_COUNT - 1 : g_settings_index - 1;
    else if(delta > 0)
        g_settings_index = (uint8_t)((g_settings_index + 1) % SETTINGS_ROW_COUNT);

    settings_ensure_visible();
    if(previous != g_settings_index)
    {
        if(previous_top != g_settings_top)
            settings_screen_render();
        else
        {
            settings_draw_row(previous);
            settings_draw_row(g_settings_index);
        }
    }
    printf("[SETTINGS] select %s\r\n", settings_model_row_title(g_settings_index));
}

static void settings_toggle_led(void)
{
    settings_draw_row(settings_action_toggle_led());
}

static void settings_toggle_rgb(void)
{
    settings_draw_row(settings_action_toggle_rgb());
}

static void settings_repeat_reset(void)
{
    g_settings_repeat_button = 0;
    g_settings_repeat_ticks = 0;
}

static void settings_repeat_start(uint32_t button)
{
    g_settings_repeat_button = button;
    g_settings_repeat_ticks = SETTINGS_REPEAT_INITIAL_TICKS;
}

static void settings_adjust_value(int8_t delta)
{
    uint8_t changed_row = settings_action_adjust_value(g_settings_index, delta);
    if(changed_row != SETTINGS_ACTION_NO_ROW)
        settings_draw_row(changed_row);
}

static void settings_ok(void)
{
    if(g_settings_index == SETTINGS_LED_SWITCH)
    {
        settings_toggle_led();
        return;
    }

    if(g_settings_index == SETTINGS_RGB_SWITCH)
    {
        settings_toggle_rgb();
        return;
    }

    if(settings_action_is_editable_row(g_settings_index))
    {
        g_settings_editing = g_settings_editing ? 0 : 1;
        if(!g_settings_editing)
        {
            settings_repeat_reset();
            settings_storage_save_now();
        }
        settings_draw_row(g_settings_index);
        printf("[SETTINGS] edit %s %s\r\n", settings_model_row_title(g_settings_index), g_settings_editing ? "ON" : "OFF");
        return;
    }

    printf("[SETTINGS] version HackyLens v1.0.0\r\n");
}

void settings_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    if(input->pressed & BUTTON_BACK)
    {
        if(g_settings_editing)
        {
            g_settings_editing = 0;
            settings_repeat_reset();
            settings_storage_save_now();
            settings_draw_row(g_settings_index);
        }
        else
            shell_show_menu();
        return;
    }

    if(input->pressed & BUTTON_OK)
    {
        settings_ok();
        return;
    }

    if(g_settings_editing)
    {
        if(input->pressed & BUTTON_LEFT)
        {
            settings_adjust_value(-1);
            settings_repeat_start(BUTTON_LEFT);
        }
        if(input->pressed & BUTTON_RIGHT)
        {
            settings_adjust_value(1);
            settings_repeat_start(BUTTON_RIGHT);
        }
        return;
    }

    if(input->pressed & BUTTON_LEFT)
        settings_select_delta(-1);
    if(input->pressed & BUTTON_RIGHT)
        settings_select_delta(1);
}

void settings_controller_tick(const hk_input_snapshot_t *input)
{
    if(hk_screen_get() != SCREEN_SETTINGS || !g_settings_editing || !settings_action_is_editable_row(g_settings_index))
    {
        settings_repeat_reset();
        return;
    }

    if(g_settings_repeat_button == 0 || !(input->state & g_settings_repeat_button))
    {
        if(input->state & BUTTON_LEFT)
            settings_repeat_start(BUTTON_LEFT);
        else if(input->state & BUTTON_RIGHT)
            settings_repeat_start(BUTTON_RIGHT);
        else
            settings_repeat_reset();
        return;
    }

    if(g_settings_repeat_ticks > 0)
    {
        g_settings_repeat_ticks--;
        return;
    }

    if(g_settings_repeat_button == BUTTON_LEFT)
        settings_adjust_value(-1);
    else if(g_settings_repeat_button == BUTTON_RIGHT)
        settings_adjust_value(1);

    g_settings_repeat_ticks = SETTINGS_REPEAT_NEXT_TICKS;
}
