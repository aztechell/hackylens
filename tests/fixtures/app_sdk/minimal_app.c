#include <hackylens/app.h>

#include "minimal_private.h"

#define MINIMAL_STATE_CAPACITY (sizeof(minimal_state_t) + 64U)

static _Alignas(HK_APP_STATE_ALIGNMENT)
    uint8_t s_state_storage[MINIMAL_STATE_CAPACITY];

static minimal_state_t *minimal_state(void)
{
    return (minimal_state_t *)s_state_storage;
}

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
    if(!storage || size_bytes != sizeof(minimal_state_t))
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
    hk_capability_request_t input_request = HK_INPUT_REQUEST_0_1_INIT;

    if(result != HK_OK)
        return result;
    if(hk_app_context_identity(
           ctx, &app_id, &generation, &state->owner) != HK_OK ||
       !app_id || generation == 0U || hk_owner_is_zero(state->owner))
        return HK_ERR_INTERNAL;
    if(hk_app_context_time(ctx, 0U, &state->time) != HK_OK ||
       hk_app_context_input(ctx, 0U, &state->input) != HK_OK ||
       hk_input_acquire(
           state->owner, &input_request, &state->input_second) != HK_OK ||
       hk_app_context_display(ctx, 0U, &state->display) != HK_OK ||
       hk_app_context_service(
           ctx, "hackylens.service.fixture", &state->service) != HK_OK)
        return HK_ERR_INTERNAL;
    state->consume_input = 1U;
    return HK_OK;
}

static hk_result_t minimal_start(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
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

        hk_input_event_t queued_second;

        if(state->consume_input &&
           (hk_input_next_event(
                state->owner, &state->input, &queued) != HK_OK ||
            hk_input_next_event(
                state->owner, &state->input_second, &queued_second) != HK_OK ||
           hk_input_get_state(
               state->owner, &state->input, &input_state) != HK_OK ||
           queued.sequence != event->data.input.sequence ||
            queued_second.sequence != queued.sequence ||
            input_state != event->data.input.state))
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
       hk_input_release(
           state->owner, deadline, &state->input_second) != HK_OK ||
       hk_input_release(state->owner, deadline, &state->input) != HK_OK ||
       hk_time_release(state->owner, deadline, &state->time) != HK_OK)
        return HK_ERR_INTERNAL;
    return HK_OK;
}

const hk_app_v2_entry_t minimal_app_entry = {
    .state_storage = s_state_storage,
    .state_capacity_bytes = sizeof(s_state_storage),
    .probe = minimal_probe,
    .prepare = minimal_prepare,
    .start = minimal_start,
    .event = minimal_event,
    .tick = minimal_tick,
    .render = minimal_render,
    .stop = minimal_stop,
    .cleanup = minimal_cleanup,
};

void minimal_app_set_consume_input(uint8_t consume)
{
    minimal_state()->consume_input = consume ? 1U : 0U;
}

int minimal_app_check_input_overflow(uint32_t expected_dropped)
{
    minimal_state_t *state = minimal_state();
    hk_input_event_t first;
    hk_input_event_t second;

    if(hk_input_next_event(state->owner, &state->input, &first) !=
           HK_ERR_OVERFLOW ||
       hk_input_next_event(state->owner, &state->input_second, &second) !=
           HK_ERR_OVERFLOW ||
       first.dropped != expected_dropped ||
       second.dropped != expected_dropped || first.state != second.state ||
       hk_input_next_event(state->owner, &state->input, &first) != HK_PENDING ||
       hk_input_next_event(state->owner, &state->input_second, &second) !=
           HK_PENDING)
        return 0;
    return 1;
}

int minimal_app_check_time_contract(void)
{
    minimal_state_t *state = minimal_state();
    hk_time_t copied = state->time;
    hk_deadline_t deadline;
    uint64_t now;

    if(hk_time_deadline_after_us(
           state->owner, &state->time, HK_TIME_MAX_SLEEP_US + 1U,
           &deadline) != HK_ERR_LIMIT ||
       hk_time_release(
           state->owner, (hk_deadline_t){UINT64_MAX}, &copied) !=
           HK_ERR_INVALID_ARGUMENT ||
       hk_time_now_us(state->owner, &state->time, &now) != HK_OK)
        return 0;
    return 1;
}

int minimal_app_check_display_contract(void)
{
    minimal_state_t *state = minimal_state();
    hk_display_t copied = state->display;
    hk_display_rect_t empty = {INT32_MAX, INT32_MAX, 0U, 0U};
    hk_display_rect_t overflow = {INT32_MAX, 0, 1U, 1U};
    hk_display_rect_t visible = {0, 0, 1U, 1U};
    hk_display_rect_t outside_clip = {2, 2, 1U, 1U};

    if(hk_display_release(
           state->owner, (hk_deadline_t){UINT64_MAX}, &copied) !=
           HK_ERR_INVALID_ARGUMENT ||
       hk_display_abort(state->owner, &state->display) !=
           HK_ERR_INVALID_STATE ||
       hk_display_fill_rect(
           state->owner, &state->display, &visible, 0U) !=
           HK_ERR_INVALID_STATE ||
       hk_display_begin_batch(state->owner, &state->display) != HK_OK ||
       hk_display_set_clip(state->owner, &state->display, NULL) != HK_OK ||
       hk_display_set_clip(
           state->owner, &state->display, &visible) != HK_OK ||
       hk_display_set_clip(
           state->owner, &state->display, &overflow) !=
           HK_ERR_INVALID_ARGUMENT ||
       hk_display_fill_rect(
           state->owner, &state->display, &outside_clip, 0U) != HK_OK ||
       hk_display_fill_rect(
           state->owner, &state->display, &empty, 0U) != HK_OK ||
       hk_display_fill_rect(
           state->owner, &state->display, &overflow, 0U) !=
           HK_ERR_INVALID_ARGUMENT ||
       hk_display_fill_rect(
           state->owner, &state->display, &visible, 0U) != HK_OK ||
       hk_display_present(
           state->owner, &state->display,
           (hk_deadline_t){UINT64_MAX}, NULL) != HK_ERR_INVALID_ARGUMENT ||
       hk_display_abort(state->owner, &state->display) != HK_OK ||
       hk_display_abort(state->owner, &state->display) !=
           HK_ERR_INVALID_STATE)
        return 0;
    return 1;
}

int minimal_app_check_stale_reacquire(void)
{
    minimal_state_t *state = minimal_state();
    hk_input_t stale = state->input_second;
    hk_capability_request_t request = HK_INPUT_REQUEST_0_1_INIT;
    hk_deadline_t deadline;
    uint32_t input_state;

    if(hk_time_deadline_after_us(
           state->owner, &state->time, 1U, &deadline) != HK_OK ||
       hk_input_release(
           state->owner, deadline, &state->input_second) != HK_OK ||
       hk_input_acquire(
           state->owner, &request, &state->input_second) != HK_OK ||
       hk_input_get_state(state->owner, &stale, &input_state) !=
           HK_ERR_STALE_HANDLE ||
       hk_input_get_state(
           state->owner, &state->input_second, &input_state) != HK_OK)
        return 0;
    return 1;
}
