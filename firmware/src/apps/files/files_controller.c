#include "files_controller.h"

#include <stdio.h>
#include <string.h>

#include "files_config.h"
#include "file_browser_mode.h"
#include "files_actions.h"
#include "files_presenter.h"

static uint32_t g_files_repeat_button;
static uint64_t g_files_repeat_due_us;
static uint64_t g_files_ok_press_us;
static uint8_t g_files_ok_active;
static uint8_t g_files_ok_hold_fired;

static uint64_t files_time_now_us(files_state_t *state)
{
    uint64_t value = 0U;

    if(!state || !state->time)
        return 0U;
    if(hk_time_now_us(state->time, &value) != HK_OK)
        return 0U;
    return value;
}

static void files_controller_reset_input(void)
{
    g_files_repeat_button = 0;
    g_files_repeat_due_us = 0;
    g_files_ok_press_us = 0;
    g_files_ok_active = 0;
    g_files_ok_hold_fired = 0;
}

static void files_repeat_reset(void)
{
    g_files_repeat_button = 0;
    g_files_repeat_due_us = 0;
}

static void files_repeat_start(uint32_t button, uint64_t now_us)
{
    g_files_repeat_button = button;
    g_files_repeat_due_us = now_us ? now_us + FILES_REPEAT_INITIAL_US : 0U;
}

static void files_ok_reset(void)
{
    g_files_ok_press_us = 0;
    g_files_ok_active = 0;
    g_files_ok_hold_fired = 0;
}

static void files_ok_press(uint64_t now_us)
{
    g_files_ok_press_us = now_us;
    g_files_ok_active = 1U;
    g_files_ok_hold_fired = 0;
}

static uint8_t files_ok_release_should_open(void)
{
    return g_files_ok_active && !g_files_ok_hold_fired;
}

static uint8_t files_ok_hold_ready(uint32_t input_state, uint64_t now_us)
{
    if(!g_files_ok_active ||
       g_files_ok_press_us == 0U ||
       now_us == 0U ||
       now_us < g_files_ok_press_us ||
       g_files_ok_hold_fired ||
       !(input_state & HK_INPUT_BUTTON_OK) ||
       now_us - g_files_ok_press_us < FILES_OK_HOLD_US)
        return 0;

    g_files_ok_hold_fired = 1;
    g_files_ok_active = 0U;
    g_files_ok_press_us = 0;
    return 1;
}

static void files_controller_handle_nav_repeat(uint32_t input_state, uint64_t now_us)
{
    if(g_files_repeat_button == 0 || !(input_state & g_files_repeat_button))
    {
        if(input_state & HK_INPUT_BUTTON_LEFT)
            files_repeat_start(HK_INPUT_BUTTON_LEFT, now_us);
        else if(input_state & HK_INPUT_BUTTON_RIGHT)
            files_repeat_start(HK_INPUT_BUTTON_RIGHT, now_us);
        else
            files_repeat_reset();
        return;
    }

    if(!now_us || !g_files_repeat_due_us || now_us < g_files_repeat_due_us)
        return;

    if(g_files_repeat_button == HK_INPUT_BUTTON_LEFT)
        files_nav_delta(-1);
    else if(g_files_repeat_button == HK_INPUT_BUTTON_RIGHT)
        files_nav_delta(1);

    /* One step only, even after a slow frame; never catch up missed repeats. */
    g_files_repeat_due_us = now_us + FILES_REPEAT_NEXT_US;
}

static void files_controller_tick_delete(uint32_t input_state, uint64_t now_us)
{
    file_browser_mode_t mode = files_mode();

    if(mode == FILES_MODE_DELETE_CONFIRM)
    {
        files_repeat_reset();
        return;
    }

    if((mode == FILES_MODE_LIST ||
        mode == FILES_MODE_TEXT ||
        mode == FILES_MODE_IMAGE) &&
       files_ok_hold_ready(input_state, now_us))
    {
        files_repeat_reset();
        files_delete_confirm_enter();
        return;
    }

    files_controller_handle_nav_repeat(input_state, now_us);
}

static void files_controller_handle_delete_confirm(uint32_t pressed)
{
    if(pressed & HK_INPUT_BUTTON_BACK)
    {
        files_repeat_reset();
        files_ok_reset();
        files_delete_cancel();
        return;
    }
    if(pressed & HK_INPUT_BUTTON_OK)
    {
        files_repeat_reset();
        files_ok_reset();
        files_delete_confirmed();
    }
}

static void files_controller_handle_preview_buttons(
    uint32_t pressed, uint32_t changed, uint32_t input_state, uint64_t now_us)
{
    if(pressed & HK_INPUT_BUTTON_BACK)
    {
        files_repeat_reset();
        files_presenter_close_image();
        files_set_mode(FILES_MODE_LIST);
        files_ok_reset();
        files_presenter_render_list();
        return;
    }
    if(pressed & HK_INPUT_BUTTON_LEFT)
    {
        files_nav_delta(-1);
        files_repeat_start(HK_INPUT_BUTTON_LEFT, now_us);
    }
    if(pressed & HK_INPUT_BUTTON_RIGHT)
    {
        files_nav_delta(1);
        files_repeat_start(HK_INPUT_BUTTON_RIGHT, now_us);
    }
    if(pressed & HK_INPUT_BUTTON_OK)
    {
        files_repeat_reset();
        files_ok_press(now_us);
        return;
    }
    if((changed & HK_INPUT_BUTTON_OK) && !(input_state & HK_INPUT_BUTTON_OK))
    {
        if(files_ok_release_should_open() && files_mode() == FILES_MODE_IMAGE)
            (void)files_presenter_toggle_image_pause(now_us);
        files_ok_reset();
    }
}

static uint8_t files_controller_handle_list_back(void)
{
    files_repeat_reset();
    files_ok_reset();
    return files_back_from_list();
}

static void files_controller_handle_list_buttons(
    uint32_t pressed, uint32_t changed, uint32_t input_state, uint64_t now_us)
{
    if(pressed & HK_INPUT_BUTTON_LEFT)
    {
        files_nav_delta(-1);
        files_repeat_start(HK_INPUT_BUTTON_LEFT, now_us);
    }
    if(pressed & HK_INPUT_BUTTON_RIGHT)
    {
        files_nav_delta(1);
        files_repeat_start(HK_INPUT_BUTTON_RIGHT, now_us);
    }
    if(pressed & HK_INPUT_BUTTON_OK)
    {
        files_repeat_reset();
        files_ok_press(now_us);
        return;
    }
    if((changed & HK_INPUT_BUTTON_OK) && !(input_state & HK_INPUT_BUTTON_OK))
    {
        if(files_ok_release_should_open())
            files_open_selected();
        files_ok_reset();
    }
}

static void files_controller_handle_buttons(
    files_state_t *state, uint32_t state_bits, uint32_t pressed, uint32_t changed)
{
    uint64_t now_us = files_time_now_us(state);

    if(files_mode())
    {
        if(files_mode() == FILES_MODE_DELETE_CONFIRM)
        {
            files_controller_handle_delete_confirm(pressed);
            return;
        }

        files_controller_handle_preview_buttons(
            pressed, changed, state_bits, now_us);
        return;
    }

    if(pressed & HK_INPUT_BUTTON_BACK)
    {
        if(files_controller_handle_list_back())
            state->close_requested = 1U;
        return;
    }

    files_controller_handle_list_buttons(pressed, changed, state_bits, now_us);
}

void files_controller_reset(files_state_t *state)
{
    if(!state)
        return;
    memset(state, 0, sizeof(*state));
    files_controller_reset_input();
}

void files_controller_enter(files_state_t *state)
{
    files_presenter_bind_input(state->owner, &state->input);
    printf("[SHELL] screen FILES\r\n");
    files_controller_reset_input();
    files_backend_enter();
}

void files_controller_exit(files_state_t *state)
{
    (void)state;
    files_presenter_close_image();
    files_presenter_bind_input(HK_OWNER_NONE, NULL);
    files_controller_reset_input();
}

void files_controller_handle_input(
    files_state_t *state, const hk_input_event_t *event)
{
    if(!state || !event)
        return;
    files_controller_handle_buttons(
        state, event->state, event->pressed, event->changed);
}

void files_controller_tick(files_state_t *state, uint32_t buttons)
{
    uint64_t now_us;

    if(!state)
        return;
    now_us = files_time_now_us(state);
    files_controller_tick_delete(buttons, now_us);
}

void files_controller_poll_animation(files_state_t *state)
{
    if(state && files_mode() == FILES_MODE_IMAGE)
        files_presenter_tick_image(files_time_now_us(state));
}

void files_controller_handle_media(
    files_state_t *state, hk_app_media_kind_t kind)
{
    hk_sd_event_t event = HK_SD_EVENT_ERROR;

    (void)state;
    files_controller_reset_input();
    if(kind == HK_APP_MEDIA_INSERTED)
        event = HK_SD_EVENT_INSERTED;
    else if(kind == HK_APP_MEDIA_REMOVED)
        event = HK_SD_EVENT_REMOVED;
    else if(kind == HK_APP_MEDIA_MOUNTED)
        event = HK_SD_EVENT_MOUNTED;
    files_refresh_after_sd_event(event);
}
