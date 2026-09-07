#include "input_normative_backend.h"

#include <hackylens/capability/input.h>

#include "../firmware/src/capabilities/input_provider.h"
#include "../firmware/src/capabilities/input_state.h"

static hk_input_state_t s_state;

static hk_result_t fake_open(void *context, hk_input_cursor_t *cursor)
{
    return hk_input_state_open_cursor((hk_input_state_t *)context, cursor);
}

static hk_result_t fake_info(void *context, hk_input_info_t *info)
{
    (void)context;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_input_info_t){
        sizeof(*info), HK_INPUT_INFO_VERSION, HK_INPUT_BUTTON_ALL,
        HK_INPUT_SAMPLE_INTERVAL_US, HK_INPUT_DEBOUNCE_INTERVAL_US,
        HK_INPUT_EVENT_CAPACITY, 0U,
    };
    return HK_OK;
}

static hk_result_t fake_state(void *context, uint32_t *state)
{
    return hk_input_state_get((hk_input_state_t *)context, state);
}

static hk_result_t fake_event(
    void *context, hk_input_cursor_t *cursor, hk_input_event_t *event)
{
    return hk_input_state_next_event(
        (hk_input_state_t *)context, cursor, event);
}

const hk_input_t hk_input_binding = {
    .context = &s_state,
    .open_cursor = fake_open,
    .get_info = fake_info,
    .get_state = fake_state,
    .next_event = fake_event,
};

const char *input_normative_backend_name(void)
{
    return "fake";
}

void input_normative_backend_reset(void)
{
    hk_input_state_reset(&s_state);
    (void)hk_input_state_sample(&s_state, 0U, 0U);
}

hk_result_t input_normative_backend_sample(
    uint64_t timestamp_us, uint32_t raw_state)
{
    return hk_input_state_sample(&s_state, timestamp_us, raw_state);
}
