#include "buttons_app.h"

#include <stdio.h>

#include "buttons_controller.h"
#include "buttons_view.h"

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state_storage[1024];

_Static_assert(
    sizeof(buttons_state_t) <= sizeof(s_state_storage),
    "BUTTONS state must fit the v2 storage slot");

static hk_result_t buttons_state_from(
    const hk_app_context_t *ctx, buttons_state_t **state)
{
    void *storage = NULL;
    uint32_t size_bytes = 0U;
    hk_result_t result;

    if(!state)
        return HK_ERR_INVALID_ARGUMENT;
    result = hk_app_context_state(ctx, &storage, &size_bytes);
    if(result != HK_OK)
        return result;
    if(!storage || size_bytes < sizeof(buttons_state_t))
        return HK_ERR_LIMIT;
    *state = (buttons_state_t *)storage;
    return HK_OK;
}

static hk_result_t buttons_finish_work(
    const hk_app_context_t *ctx, buttons_state_t *state)
{
    hk_result_t result = HK_OK;

    if(state->dirty)
    {
        result = hk_app_context_request_render(ctx, NULL);
        if(result != HK_OK)
            return result;
        state->dirty = 0U;
    }
    if(state->close_requested)
        return hk_app_context_request_close(ctx);
    return HK_OK;
}

static hk_result_t buttons_start(const hk_app_context_t *ctx)
{
    buttons_state_t *state = NULL;
    const char *app_id = NULL;
    uint32_t generation = 0U;
    uint32_t buttons = 0U;
    hk_owner_t owner = HK_OWNER_NONE;
    hk_input_t input = {0};
    hk_result_t result = buttons_state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(hk_app_context_identity(ctx, &app_id, &generation, &owner) != HK_OK ||
       !app_id || generation == 0U || hk_owner_is_zero(owner) ||
       hk_app_context_input(ctx, 0U, &input) != HK_OK)
        return HK_ERR_INTERNAL;
    if(hk_input_get_state(owner, &input, &buttons) != HK_OK)
        return HK_ERR_INTERNAL;
    buttons_controller_reset(state, buttons);
    state->owner = owner;
    state->input = input;
    printf("[SHELL] screen BUTTONS\r\n");
    return HK_OK;
}

static hk_result_t buttons_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    buttons_state_t *state = NULL;
    uint32_t buttons = 0U;
    hk_result_t result = buttons_state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(!event)
        return HK_ERR_INVALID_ARGUMENT;
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        buttons_controller_handle_input(state, &event->data.input);
        return buttons_finish_work(ctx, state);
    }
    if(event->kind == HK_APP_EVENT_TIMER)
    {
        if(hk_input_get_state(state->owner, &state->input, &buttons) != HK_OK)
            return HK_ERR_INTERNAL;
        buttons_controller_tick(state, buttons);
        return buttons_finish_work(ctx, state);
    }
    return HK_OK;
}

static hk_result_t buttons_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    buttons_state_t *state = NULL;
    hk_result_t result = buttons_state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    return buttons_view_render(surface, &state->view);
}

static hk_result_t buttons_stop(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline;

    return hk_app_context_teardown_deadline(ctx, &deadline);
}

const hk_app_v2_entry_t buttons_v2_entry = {
    .state_storage = s_state_storage,
    .state_capacity_bytes = sizeof(s_state_storage),
    .start = buttons_start,
    .event = buttons_event,
    .render = buttons_render,
    .stop = buttons_stop,
};
