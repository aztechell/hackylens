#include "power_off_app.h"
#include <hackylens/capability/power.h>
#include <hackylens/capability/time.h>
#include <string.h>

typedef struct {
    hk_deadline_t deadline;
    uint8_t armed;
    uint8_t waiting;
    uint8_t failed;
    uint8_t accepted;
} power_off_state_t;
static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_storage[1024];
static hk_result_t state_from(const hk_app_context_t *ctx, power_off_state_t **state)
{
    void *data = NULL;
    uint32_t size = 0U;
    hk_result_t result = hk_app_context_state(ctx, &data, &size);
    if(result != HK_OK) return result;
    if(!data || size < sizeof(**state)) return HK_ERR_LIMIT;
    *state = data;
    return HK_OK;
}
/* The SDK reserves .text.start for its reset entry. With function sections,
 * a C function named start would precede crt.S in the raw flash image. */
static hk_result_t power_off_start(const hk_app_context_t *ctx)
{
    power_off_state_t *state;
    const hk_input_t *input;
    uint32_t buttons;
    hk_result_t result = state_from(ctx, &state);
    if(result != HK_OK) return result;
    if(hk_app_context_input(ctx, &input) != HK_OK ||
       hk_input_get_state(input, &buttons) != HK_OK) return HK_ERR_INTERNAL;
    memset(state, 0, sizeof(*state));
    state->armed = (buttons & HK_INPUT_BUTTON_OK) == 0U;
    return HK_OK;
}
static hk_result_t event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    power_off_state_t *state;
    hk_result_t result = state_from(ctx, &state);
    if(result != HK_OK) return result;
    if(!event) return HK_ERR_INVALID_ARGUMENT;
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        if(event->data.input.pressed & HK_INPUT_BUTTON_BACK)
            return hk_app_context_request_close(ctx);
        if(event->data.input.released & HK_INPUT_BUTTON_OK) state->armed = 1U;
        if(state->armed && !state->waiting && !state->accepted &&
           (event->data.input.pressed & HK_INPUT_BUTTON_OK))
        {
            result = hk_time_deadline_after_us(hk_time_service(), 2000000U, &state->deadline);
            if(result != HK_OK) return result;
            state->waiting = 1U;
            state->failed = 0U;
        }
    }
    if(state->waiting)
    {
        result = hk_power_request_off(hk_power_service(), state->deadline);
        if(result != HK_PENDING)
        {
            state->waiting = 0U;
            state->accepted = result == HK_OK;
            state->failed = result != HK_OK;
        }
        return hk_app_context_request_render(ctx, NULL);
    }
    return HK_OK;
}
static hk_result_t text(hk_app_surface_t *surface, int y, const char *label)
{
    hk_display_rect_t bounds = {16, y, 208, 24};
    return hk_app_surface_text(surface, &bounds, label, (uint32_t)strlen(label), 0xFFFFU);
}
static hk_result_t render(const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    power_off_state_t *state;
    hk_result_t result = state_from(ctx, &state);
    if(result != HK_OK) return result;
    result = hk_app_surface_clear(surface, 0U);
    if(result != HK_OK) return result;
    result = text(surface, 44, "POWER OFF");
    if(result != HK_OK) return result;
    result = text(surface, 100, state->failed ? "FAILED - TRY AGAIN" :
                  state->waiting ? "STOPPING..." : state->accepted ? "POWERING OFF..." : "OK: POWER OFF");
    return result == HK_OK ? text(surface, 148, "BACK: CANCEL") : result;
}
static hk_result_t stop(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline;
    return hk_app_context_teardown_deadline(ctx, &deadline);
}
const hk_app_v2_entry_t power_off_v2_entry = {
    .state_storage = s_storage, .state_capacity_bytes = sizeof(s_storage),
    .start = power_off_start, .event = event, .render = render, .stop = stop,
};
