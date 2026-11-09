#include "files_app.h"

#include <stddef.h>

#include "files_controller.h"
#include "files_view.h"
#if __has_include("hk_config.h")
#include "hk_config.h"
#endif
#if HK_ENABLE_AUDIO_FEATURE
#include "file_browser_mode.h"
#include "../../services/audio_player.h"
#include "../../ui/audio_player_view.h"
#endif

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state_storage[1024];

_Static_assert(
    sizeof(files_state_t) <= sizeof(s_state_storage),
    "Files state must fit the v2 storage slot");

static hk_result_t files_state_from(
    const hk_app_context_t *ctx, files_state_t **state)
{
    void *storage = NULL;
    uint32_t size_bytes = 0U;
    hk_result_t result;

    if(!state)
        return HK_ERR_INVALID_ARGUMENT;
    result = hk_app_context_state(ctx, &storage, &size_bytes);
    if(result != HK_OK)
        return result;
    if(!storage || size_bytes < sizeof(files_state_t))
        return HK_ERR_LIMIT;
    *state = (files_state_t *)storage;
    return HK_OK;
}

static hk_result_t files_finish_work(
    const hk_app_context_t *ctx, files_state_t *state)
{
    if(state->close_requested)
        return hk_app_context_request_close(ctx);
#if HK_ENABLE_AUDIO_FEATURE
    if(files_mode() != FILES_MODE_AUDIO)
        state->player_drawn = 0U;
    else
    {
        audio_player_view_t player = audio_player_view(HK_AUDIO_OWNER_FILES);
        if(!state->player_drawn || player.position != state->player_position ||
           player.status != state->player_status || audio_service_volume() != state->player_volume ||
           files_controller_audio_error() != state->player_error)
            return audio_player_redraw(ctx, state->player_width, !state->player_drawn);
    }
#endif
    return HK_OK;
}

#if HK_ENABLE_AUDIO_FEATURE
static hk_result_t files_render(const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    if(files_mode() != FILES_MODE_AUDIO)
        return HK_OK;
    files_state_t *state;
    hk_display_info_t display;
    hk_result_t result = files_state_from(ctx, &state);
    if(result == HK_OK)
        result = hk_app_surface_get_info(surface, &display);
    if(result != HK_OK)
        return result;

    audio_player_view_t player = audio_player_view(HK_AUDIO_OWNER_FILES);
    const char *error = files_controller_audio_error();
    uint8_t volume = audio_service_volume();
    result = audio_player_draw(surface, &player, volume, error, !state->player_drawn);
    if(result == HK_OK)
    {
        state->player_drawn = 1U;
        state->player_width = display.width;
        state->player_status = player.status;
        state->player_position = player.position;
        state->player_volume = volume;
        state->player_error = error;
    }
    return result;
}
#endif

static hk_result_t files_start(const hk_app_context_t *ctx)
{
    files_state_t *state = NULL;
    const char *app_id = NULL;
    uint32_t generation = 0U;
    const hk_input_t *input = NULL;
    const hk_time_t *time = NULL;
    hk_result_t result = files_state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(hk_app_context_identity(ctx, &app_id, &generation) != HK_OK ||
       !app_id || generation == 0U ||
       hk_app_context_input(ctx, &input) != HK_OK ||
       hk_app_context_time(ctx, &time) != HK_OK)
        return HK_ERR_INTERNAL;
    files_view_init();
    files_controller_reset(state);
    state->input = input;
    state->time = time;
    files_controller_enter(state);
    return HK_OK;
}

static hk_result_t files_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    files_state_t *state = NULL;
    uint32_t buttons = 0U;
    hk_result_t result = files_state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(!event)
        return HK_ERR_INVALID_ARGUMENT;
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        files_controller_handle_input(state, &event->data.input);
        return files_finish_work(ctx, state);
    }
    if(event->kind == HK_APP_EVENT_TIMER)
    {
        if(hk_input_get_state(state->input, &buttons) != HK_OK)
            return HK_ERR_INTERNAL;
        files_controller_tick(state, buttons);
        if(!state->close_requested)
            files_controller_poll_animation(state);
        return files_finish_work(ctx, state);
    }
    if(event->kind == HK_APP_EVENT_MEDIA)
    {
        files_controller_handle_media(state, event->data.media.kind);
        return files_finish_work(ctx, state);
    }
    return HK_OK;
}

static hk_result_t files_stop(const hk_app_context_t *ctx)
{
    files_state_t *state = NULL;
    hk_deadline_t deadline;
    hk_result_t result = files_state_from(ctx, &state);

    if(result == HK_OK)
        files_controller_exit(state);
    return hk_app_context_teardown_deadline(ctx, &deadline);
}

const hk_app_v2_entry_t files_v2_entry = {
    .state_storage = s_state_storage,
    .state_capacity_bytes = sizeof(s_state_storage),
    .start = files_start,
    .event = files_event,
#if HK_ENABLE_AUDIO_FEATURE
    .render = files_render,
#endif
    .stop = files_stop,
};
