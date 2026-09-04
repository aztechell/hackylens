#include "pong_app.h"

#include <stdio.h>

#include "pong_controller.h"
#include "pong_view.h"

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state_storage[1024];

_Static_assert(
    sizeof(pong_state_t) <= sizeof(s_state_storage),
    "PONG state must fit the v2 storage slot");

static hk_result_t pong_state_from(
    const hk_app_context_t *ctx, pong_state_t **state)
{
    void *storage = NULL;
    uint32_t size_bytes = 0U;
    hk_result_t result;

    if(!state)
        return HK_ERR_INVALID_ARGUMENT;
    result = hk_app_context_state(ctx, &storage, &size_bytes);
    if(result != HK_OK)
        return result;
    if(!storage || size_bytes < sizeof(pong_state_t))
        return HK_ERR_LIMIT;
    *state = (pong_state_t *)storage;
    return HK_OK;
}

static hk_result_t pong_request_pending_render(
    const hk_app_context_t *ctx, pong_state_t *state)
{
    hk_display_rect_t regions[HK_APP_MAX_INVALIDATIONS];
    uint8_t full = 0U;
    uint8_t count;
    uint8_t index;
    hk_result_t result;
    pong_view_state_t current;

    if(state->need_full_redraw)
        return hk_app_context_request_render(ctx, NULL);
    if(!state->dirty)
        return HK_OK;
    current = pong_controller_view_state(state);
    count = pong_view_collect_invalidations(
        state->previous, current, state->score_changed,
        regions, HK_APP_MAX_INVALIDATIONS, &full);
    if(full)
        result = hk_app_context_request_render(ctx, NULL);
    else
    {
        result = HK_OK;
        for(index = 0U; result == HK_OK && index < count; index++)
            result = hk_app_context_request_render(ctx, &regions[index]);
    }
    if(result != HK_OK)
        return result;
    state->dirty = 0U;
    return HK_OK;
}

static hk_result_t pong_finish_work(
    const hk_app_context_t *ctx, pong_state_t *state)
{
    hk_result_t result;

    if(state->close_requested)
        return hk_app_context_request_close(ctx);
    result = pong_request_pending_render(ctx, state);
    if(result != HK_OK)
        return result;
    return HK_OK;
}

static hk_result_t pong_start(const hk_app_context_t *ctx)
{
    pong_state_t *state = NULL;
    const char *app_id = NULL;
    uint32_t generation = 0U;
    hk_owner_t owner = HK_OWNER_NONE;
    hk_time_t time = {0};
    hk_input_t input = {0};
    uint64_t now_us = 0U;
    hk_result_t result = pong_state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(hk_app_context_identity(ctx, &app_id, &generation, &owner) != HK_OK ||
       !app_id || generation == 0U || hk_owner_is_zero(owner) ||
       hk_app_context_time(ctx, 0U, &time) != HK_OK ||
       hk_app_context_input(ctx, 0U, &input) != HK_OK ||
       hk_time_now_us(owner, &time, &now_us) != HK_OK)
        return HK_ERR_INTERNAL;
    state->owner = owner;
    state->time = time;
    state->input = input;
    pong_controller_reset(state, now_us);
    printf("[SHELL] screen PONG\r\n");
    return HK_OK;
}

static hk_result_t pong_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    pong_state_t *state = NULL;
    uint32_t buttons = 0U;
    hk_result_t result = pong_state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(!event)
        return HK_ERR_INVALID_ARGUMENT;
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        pong_controller_handle_input(state, &event->data.input);
        return pong_finish_work(ctx, state);
    }
    if(event->kind == HK_APP_EVENT_TIMER)
    {
        if(hk_input_get_state(state->owner, &state->input, &buttons) != HK_OK)
            return HK_ERR_INTERNAL;
        pong_controller_tick(state, buttons, event->data.timer.now_us);
        return pong_finish_work(ctx, state);
    }
    return HK_OK;
}

static hk_result_t pong_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    pong_state_t *state = NULL;
    pong_view_state_t current;
    hk_result_t result = pong_state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    current = pong_controller_view_state(state);
    if(state->need_full_redraw)
        result = pong_view_render_initial(surface, current);
    else
    {
        result = pong_view_render_frame(surface, state->previous, current);
        if(result == HK_OK && state->score_changed)
            result = pong_view_render_score(surface, current);
    }
    if(result != HK_OK)
        return result;
    state->previous = current;
    state->need_full_redraw = 0U;
    state->score_changed = 0U;
    state->dirty = 0U;
    return HK_OK;
}

static hk_result_t pong_stop(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline;

    return hk_app_context_teardown_deadline(ctx, &deadline);
}

const hk_app_v2_entry_t pong_v2_entry = {
    .state_storage = s_state_storage,
    .state_capacity_bytes = sizeof(s_state_storage),
    .start = pong_start,
    .event = pong_event,
    .render = pong_render,
    .stop = pong_stop,
};
