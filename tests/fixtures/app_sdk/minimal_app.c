#include <hackylens/app.h>

#include "minimal_private.h"

static _Alignas(HK_APP_STATE_ALIGNMENT) minimal_state_t s_state;

static hk_result_t state_from(
    const hk_app_context_t *ctx,
    minimal_state_t **state)
{
    void *storage = NULL;
    uint32_t size_bytes = 0U;
    hk_result_t result;

    if(!state)
        return HK_ERR_INVALID_ARGUMENT;
    result = hk_app_context_state(ctx, &storage, &size_bytes);
    if(result != HK_OK)
        return result;
    if(!storage || size_bytes < sizeof(minimal_state_t))
        return HK_ERR_LIMIT;
    *state = (minimal_state_t *)storage;
    return HK_OK;
}

static hk_result_t minimal_probe(const hk_app_context_t *ctx)
{
    const char *app_id = NULL;
    const char *fallback = NULL;
    hk_owner_t owner = HK_OWNER_NONE;
    uint32_t generation = 0U;
    uint8_t available = 0U;

    if(hk_app_context_identity(ctx, &app_id, &generation, &owner) != HK_OK ||
       !app_id || generation == 0U || !hk_owner_is_zero(owner))
        return HK_ERR_INTERNAL;
    if(hk_app_context_capability_status(
           ctx, HK_CAPABILITY_ID_TIME, 0U, &available, &fallback) != HK_OK ||
       !available || fallback)
        return HK_ERR_INTERNAL;
    if(hk_app_context_capability_status(
           ctx, HK_CAPABILITY_ID_INPUT, 0U, &available, &fallback) != HK_OK ||
       !available || fallback)
        return HK_ERR_INTERNAL;
    if(hk_app_context_capability_status(
           ctx, HK_CAPABILITY_ID_DISPLAY, 0U, &available, &fallback) != HK_OK ||
       !available || fallback)
        return HK_ERR_INTERNAL;
    return HK_OK;
}

static hk_result_t minimal_prepare(const hk_app_context_t *ctx)
{
    minimal_state_t *state = NULL;
    const char *app_id = NULL;
    uint32_t generation = 0U;
    hk_result_t result = state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(hk_app_context_identity(
           ctx, &app_id, &generation, &state->owner) != HK_OK ||
       !app_id || generation == 0U || hk_owner_is_zero(state->owner))
        return HK_ERR_INTERNAL;
    if(hk_app_context_time(ctx, 0U, &state->time) != HK_OK ||
       hk_app_context_input(ctx, 0U, &state->input) != HK_OK ||
       hk_app_context_display(ctx, 0U, &state->display) != HK_OK ||
       hk_app_context_service(
           ctx, "hackylens.service.fixture", &state->service) != HK_OK)
        return HK_ERR_INTERNAL;
    return HK_OK;
}

static hk_result_t minimal_start(const hk_app_context_t *ctx)
{
    return hk_app_context_request_render(ctx, NULL);
}

static hk_result_t minimal_event(
    const hk_app_context_t *ctx,
    const hk_app_event_t *event)
{
    minimal_state_t *state = NULL;
    hk_result_t result = state_from(ctx, &state);

    if(result != HK_OK || !event || event->sequence == 0U)
        return result != HK_OK ? result : HK_ERR_INVALID_ARGUMENT;
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        hk_input_event_t queued;
        uint32_t input_state = 0U;

        if(hk_input_next_event(state->owner, &state->input, &queued) != HK_OK ||
           hk_input_get_state(
               state->owner, &state->input, &input_state) != HK_OK ||
           queued.sequence != event->data.input.sequence ||
           input_state != event->data.input.state)
            return HK_ERR_INTERNAL;
        state->input_events++;
    }
    else if(event->kind == HK_APP_EVENT_MEDIA)
    {
        state->media_events++;
    }
    else if(event->kind == HK_APP_EVENT_RUNTIME_CLOSE)
    {
        state->close_events++;
    }
    return HK_OK;
}

static hk_result_t minimal_tick(
    const hk_app_context_t *ctx,
    uint64_t now_us)
{
    minimal_state_t *state = NULL;
    uint64_t observed_us = 0U;
    hk_result_t result = state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(hk_time_now_us(state->owner, &state->time, &observed_us) != HK_OK ||
       observed_us != now_us)
        return HK_ERR_INTERNAL;
    state->ticks++;
    return HK_OK;
}

static hk_result_t minimal_render(
    const hk_app_context_t *ctx,
    hk_app_surface_t *surface)
{
    minimal_state_t *state = NULL;
    hk_display_info_t info = {0};
    hk_display_rect_t marker = {2, 3, 8U, 5U};
    hk_result_t result = state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(hk_app_surface_get_info(surface, &info) != HK_OK ||
       info.width == 0U || info.height == 0U ||
       hk_app_surface_clear(surface, 0U) != HK_OK ||
       hk_app_surface_fill_rect(surface, &marker, UINT16_C(0xFFFF)) != HK_OK)
        return HK_ERR_INTERNAL;
    state->renders++;
    if(state->renders == 1U)
        return hk_app_context_request_render(ctx, &marker);
    return HK_OK;
}

static hk_result_t minimal_stop(
    const hk_app_context_t *ctx,
    hk_app_stop_reason_t reason)
{
    minimal_state_t *state = NULL;
    hk_result_t result = state_from(ctx, &state);

    if(result != HK_OK || reason > HK_APP_STOP_SHUTDOWN)
        return result != HK_OK ? result : HK_ERR_INVALID_ARGUMENT;
    return hk_app_context_teardown_deadline(ctx, &state->stop_deadline);
}

static hk_result_t minimal_cleanup(const hk_app_context_t *ctx)
{
    minimal_state_t *state = NULL;
    hk_deadline_t deadline;
    hk_result_t result = state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(hk_app_context_teardown_deadline(ctx, &deadline) != HK_OK ||
       deadline.at_us != state->stop_deadline.at_us)
        return HK_ERR_INTERNAL;
    if(hk_display_release(state->owner, deadline, &state->display) != HK_OK ||
       hk_input_release(state->owner, deadline, &state->input) != HK_OK ||
       hk_time_release(state->owner, deadline, &state->time) != HK_OK)
        return HK_ERR_INTERNAL;
    return HK_OK;
}

const hk_app_v2_entry_t minimal_app_entry = {
    .state_storage = &s_state,
    .state_capacity_bytes = sizeof(s_state),
    .probe = minimal_probe,
    .prepare = minimal_prepare,
    .start = minimal_start,
    .event = minimal_event,
    .tick = minimal_tick,
    .render = minimal_render,
    .stop = minimal_stop,
    .cleanup = minimal_cleanup,
};
