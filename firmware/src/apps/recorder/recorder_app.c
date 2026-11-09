#include "recorder_app.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <hackylens/app/panel.h>
#include <hackylens/app/recorder.h>

#define RECORDER_LIST_ROWS 6U
#define RECORDER_RED 0xf800U

typedef enum
{
    RECORDER_MENU,
    RECORDER_CAPTURE,
    RECORDER_LIST,
    RECORDER_VOLUME,
    RECORDER_PLAYER,
} recorder_screen_t;

typedef struct
{
    recorder_screen_t screen;
    uint8_t menu_index;
    uint8_t file_index;
    uint8_t full_redraw;
    uint8_t last_volume;
    uint16_t width;
    uint32_t last_seconds;
    char last_status[32];
    char message[32];
} recorder_t;

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_storage[1024];
_Static_assert(sizeof(recorder_t) <= sizeof(s_storage), "Recorder state is too large");

static recorder_t *recorder_state(const hk_app_context_t *ctx)
{
    void *state = NULL;
    uint32_t size = 0U;
    return hk_app_context_state(ctx, &state, &size) == HK_OK &&
           size >= sizeof(recorder_t) ? state : NULL;
}

static void copy_status(char text[32], const char *status)
{
    (void)snprintf(text, 32U, "%s", status ? status : "");
}

static void choice(hk_app_panel_t *panel, uint16_t y, const char *text, uint8_t selected)
{
    if(selected)
        hk_app_panel_fill(panel, 6U, y, panel->width - 12U, 26U, HK_PANEL_GREEN);
    hk_app_panel_label(panel, 10U, y + 1U, panel->width - 20U, text,
                       selected ? HK_PANEL_BLACK : HK_PANEL_GREEN, HK_PANEL_LEFT);
}

static void draw_capture(hk_app_panel_t *panel, const hk_recorder_info_t *info, uint8_t full)
{
    char time[16];
    if(!full)
        hk_app_panel_fill(panel, 2U, 44U, panel->width - 4U, 132U, HK_PANEL_BLACK);
    uint16_t color = info->preparing || info->saving ? HK_PANEL_GREEN : RECORDER_RED;
    hk_app_panel_fill(panel, panel->width / 2U - 4U, 52U, 8U, 8U, color);
    (void)snprintf(time, sizeof(time), "%02lu:%02lu",
                   (unsigned long)(info->recorded_seconds / 60U),
                   (unsigned long)(info->recorded_seconds % 60U));
    hk_app_panel_label(panel, 12U, 81U, panel->width - 24U, time,
                       HK_PANEL_GREEN, HK_PANEL_CENTER);
    hk_app_panel_status(panel, 123U, info->record_status);
}

static void draw_recordings(const hk_app_context_t *ctx, hk_app_panel_t *panel,
                            const recorder_t *state, uint8_t count)
{
    uint8_t page = state->file_index / RECORDER_LIST_ROWS;
    uint8_t top = page * RECORDER_LIST_ROWS;
    for(uint8_t row = 0U; row < RECORDER_LIST_ROWS && top + row < count; row++)
        choice(panel, 37U + row * 28U, hk_recorder_name(ctx, (uint8_t)(top + row)),
               top + row == state->file_index);
    if(!count)
        hk_app_panel_status(panel, 92U, *state->message ? state->message : "NO RECORDINGS");
    else if(*state->message || count > RECORDER_LIST_ROWS)
    {
        char pages[12];
        (void)snprintf(pages, sizeof(pages), "%u/%u", (unsigned)page + 1U,
                       (unsigned)(count + RECORDER_LIST_ROWS - 1U) / RECORDER_LIST_ROWS);
        hk_app_panel_label(panel, 12U, 208U, panel->width - 24U,
                           *state->message ? state->message : pages,
                           HK_PANEL_GREEN, HK_PANEL_CENTER);
    }
}

static hk_result_t recorder_render(const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    static const char *const choices[] = {"RECORD", "RECORDINGS", "VOLUME"};
    recorder_t *state = recorder_state(ctx);
    if(!state)
        return HK_ERR_INVALID_STATE;
    hk_app_panel_t panel = hk_app_panel_begin(surface);
    if(panel.result != HK_OK)
        return panel.result;
    state->width = panel.width;
    hk_recorder_info_t info;
    hk_result_t result = hk_recorder_info(ctx, &info);
    if(result != HK_OK)
        return result;

    if(state->screen == RECORDER_PLAYER)
    {
        result = hk_recorder_player_render(ctx, surface, state->message, state->full_redraw);
        if(result != HK_OK)
            return result;
    }
    else
    {
        if(state->full_redraw)
            hk_app_panel_chrome(&panel, state->screen == RECORDER_LIST ? "RECORDINGS" :
                                state->screen == RECORDER_VOLUME ? "VOLUME" : "RECORDER");
        switch(state->screen)
        {
        case RECORDER_MENU:
            for(uint8_t index = 0U; index < 3U; index++)
                choice(&panel, 40U + index * 30U, choices[index], index == state->menu_index);
            hk_app_panel_status(&panel, 149U, state->message);
            break;
        case RECORDER_CAPTURE:
            draw_capture(&panel, &info, state->full_redraw);
            break;
        case RECORDER_LIST:
            draw_recordings(ctx, &panel, state, info.count);
            break;
        case RECORDER_VOLUME:
            hk_app_panel_volume(&panel, 84U, info.volume);
            hk_app_panel_status(&panel, 151U, state->message);
            break;
        case RECORDER_PLAYER:
            break;
        }
    }
    if(panel.result == HK_OK)
    {
        state->full_redraw = 0U;
        state->last_volume = info.volume;
        state->last_seconds = state->screen == RECORDER_PLAYER ?
            info.player_position : info.recorded_seconds;
        copy_status(state->last_status, state->screen == RECORDER_PLAYER ?
                    info.player_status : info.record_status);
    }
    return panel.result;
}

static hk_result_t redraw(const hk_app_context_t *ctx, recorder_t *state, uint8_t full)
{
    if(full)
        state->full_redraw = 1U;
    if(state->screen == RECORDER_PLAYER)
        return hk_recorder_player_redraw(ctx, state->width, state->full_redraw);
    const hk_display_rect_t dynamic = {2, 44, state->width > 4U ? state->width - 4U : 1U, 132U};
    return hk_app_context_request_render(ctx, state->full_redraw ? NULL : &dynamic);
}

static hk_result_t recorder_start(const hk_app_context_t *ctx)
{
    recorder_t *state = recorder_state(ctx);
    if(!state)
        return HK_ERR_INVALID_STATE;
    *state = (recorder_t){.full_redraw = 1U};
    return HK_OK;
}

static hk_result_t recorder_buttons(const hk_app_context_t *ctx, recorder_t *state,
                                    uint32_t pressed)
{
    const uint8_t back = (pressed & HK_INPUT_BUTTON_BACK) != 0U;
    const uint8_t ok = (pressed & HK_INPUT_BUTTON_OK) != 0U;
    const int8_t direction = pressed & HK_INPUT_BUTTON_LEFT ? -1 :
        (pressed & HK_INPUT_BUTTON_RIGHT ? 1 : 0);
    if(!pressed)
        return HK_OK;
    recorder_screen_t previous_screen = state->screen;
    state->message[0] = 0;
    hk_recorder_info_t info;
    hk_result_t result = hk_recorder_info(ctx, &info);
    if(result != HK_OK)
        return result;
    switch(state->screen)
    {
    case RECORDER_MENU:
        if(back)
            return hk_app_context_request_close(ctx);
        if(direction)
            state->menu_index = (uint8_t)((state->menu_index + 3 + direction) % 3);
        else if(ok && state->menu_index == 0U)
        {
            copy_status(state->message, hk_recorder_action(ctx, HK_RECORDER_START, 0));
            result = hk_recorder_info(ctx, &info);
            if(result != HK_OK)
                return result;
            if(info.recording)
            {
                state->screen = RECORDER_CAPTURE;
                state->message[0] = 0;
            }
        }
        else if(ok && state->menu_index == 1U)
        {
            copy_status(state->message, hk_recorder_action(ctx, HK_RECORDER_LIST, 0));
            state->file_index = 0U;
            state->screen = RECORDER_LIST;
        }
        else if(ok)
            state->screen = RECORDER_VOLUME;
        break;
    case RECORDER_CAPTURE:
        if(ok || back)
            (void)hk_recorder_action(ctx, HK_RECORDER_SAVE, 0);
        else
            return HK_OK;
        break;
    case RECORDER_LIST:
        if(back)
            state->screen = RECORDER_MENU;
        else if(info.count && direction)
            state->file_index = (uint8_t)((state->file_index + info.count + direction) % info.count);
        else if(info.count && ok)
        {
            const char *error = hk_recorder_action(ctx, HK_RECORDER_OPEN, state->file_index);
            if(error)
                copy_status(state->message, error);
            else
                state->screen = RECORDER_PLAYER;
        }
        break;
    case RECORDER_VOLUME:
        if(back || ok)
            state->screen = RECORDER_MENU;
        else if(direction)
            copy_status(state->message, hk_recorder_action(ctx, HK_RECORDER_ADJUST_VOLUME, direction));
        break;
    case RECORDER_PLAYER:
        if(back)
        {
            (void)hk_recorder_action(ctx, HK_RECORDER_CLOSE_PLAYER, 0);
            state->screen = RECORDER_LIST;
        }
        else if(ok)
            (void)hk_recorder_action(ctx, HK_RECORDER_PLAY_STOP, 0);
        else if(direction)
            copy_status(state->message, hk_recorder_action(ctx, HK_RECORDER_ADJUST_VOLUME, direction));
        break;
    }
    return redraw(ctx, state, previous_screen != state->screen ||
                  (state->screen != RECORDER_CAPTURE && state->screen != RECORDER_PLAYER));
}

static hk_result_t recorder_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    recorder_t *state = recorder_state(ctx);
    if(!state || !event)
        return HK_ERR_INVALID_ARGUMENT;
    if(event->kind == HK_APP_EVENT_INPUT)
        return recorder_buttons(ctx, state, event->data.input.pressed);
    if(event->kind == HK_APP_EVENT_MEDIA && event->data.media.kind == HK_APP_MEDIA_MOUNTED)
    {
        state->message[0] = 0;
        return redraw(ctx, state, 1U);
    }
    if(event->kind == HK_APP_EVENT_MEDIA &&
       (event->data.media.kind == HK_APP_MEDIA_REMOVED ||
        event->data.media.kind == HK_APP_MEDIA_ERROR))
    {
        (void)hk_recorder_action(ctx, HK_RECORDER_MEDIA_REMOVED, 0);
        state->screen = RECORDER_MENU;
        copy_status(state->message, "SD CARD REMOVED");
        return redraw(ctx, state, 1U);
    }
    if(event->kind != HK_APP_EVENT_TIMER ||
       (state->screen != RECORDER_CAPTURE && state->screen != RECORDER_PLAYER))
        return HK_OK;

    hk_recorder_info_t info;
    hk_result_t result = hk_recorder_info(ctx, &info);
    if(result != HK_OK)
        return result;
    if(state->screen == RECORDER_CAPTURE && !info.recording)
    {
        state->screen = RECORDER_MENU;
        copy_status(state->message, info.record_status);
        return redraw(ctx, state, 1U);
    }
    uint32_t seconds = state->screen == RECORDER_CAPTURE ? info.recorded_seconds : info.player_position;
    const char *status = state->screen == RECORDER_CAPTURE ? info.record_status : info.player_status;
    if(strcmp(state->last_status, status ? status : "") || seconds != state->last_seconds ||
       (state->screen == RECORDER_PLAYER && info.volume != state->last_volume))
        return redraw(ctx, state, 0U);
    return HK_OK;
}

static hk_result_t recorder_stop(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline;
    hk_result_t result = hk_app_context_teardown_deadline(ctx, &deadline);
    return result == HK_OK ? hk_recorder_retire(ctx, deadline) : result;
}

const hk_app_v2_entry_t recorder_v2_entry = {
    .state_storage = s_storage,
    .state_capacity_bytes = sizeof(s_storage),
    .start = recorder_start,
    .event = recorder_event,
    .render = recorder_render,
    .stop = recorder_stop,
};
