#include "../../../firmware/src/capabilities/input_provider.h"
#include "../../../firmware/src/capabilities/input_state.h"
#include "../../../firmware/src/drivers/hk_input.h"

#include <hackylens/capability/input.h>

#include "../hal/hal_time.h"

#if defined(__has_include)
#if __has_include("board_config.h")
#include "board_config.h"
#endif
#endif
#ifndef HK_BOARD_INPUT_PROFILE_CUBE
#define HK_BOARD_INPUT_PROFILE_CUBE 0
#endif

#if HK_BOARD_INPUT_PROFILE_CUBE
#include "../../../firmware/src/drivers/axp173.h"
/* PMIC reports a short-press edge rather than a physical GPIO level. */
#define CUBE_BACK_PULSE_US (HK_INPUT_DEBOUNCE_INTERVAL_US + 4U * HK_INPUT_SAMPLE_INTERVAL_US)
#endif

typedef struct
{
    hk_input_state_t state;
#if HK_BOARD_INPUT_PROFILE_CUBE
    uint64_t power_back_until_us;
#endif
} k210_input_state_t;

static k210_input_state_t s_input;

#if HK_BOARD_INPUT_PROFILE_CUBE
static uint32_t cube_logical_buttons(
    k210_input_state_t *input, uint64_t now, uint32_t raw)
{
    uint32_t logical = raw & (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_RIGHT |
                              HK_INPUT_BUTTON_OK);
    if(axp173_poll_back_press(now))
        input->power_back_until_us = now + CUBE_BACK_PULSE_US;
    if(now < input->power_back_until_us)
        logical |= HK_INPUT_BUTTON_BACK;
    return logical;
}
#endif

static hk_result_t k210_input_service(k210_input_state_t *input)
{
    uint64_t now;
    hk_result_t result;

    if(!input)
        return HK_ERR_INVALID_ARGUMENT;
    now = hal_time_us();
    if(input->state.initialized && now < input->state.next_sample_us)
        return HK_OK;
    {
        uint32_t raw = buttons_read_pressed_mask();
#if HK_BOARD_INPUT_PROFILE_CUBE
        raw = cube_logical_buttons(input, now, raw);
#endif
        result = hk_input_state_sample(&input->state, now, raw);
    }
    return result == HK_PENDING ? HK_OK : result;
}

static hk_result_t k210_input_open_cursor(
    void *context, hk_input_cursor_t *cursor)
{
    k210_input_state_t *input = (k210_input_state_t *)context;
    hk_result_t result = k210_input_service(input);

    if(result != HK_OK)
        return result;
    return hk_input_state_open_cursor(&input->state, cursor);
}

static hk_result_t k210_input_get_info(
    void *context, hk_input_info_t *info)
{
    (void)context;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_input_info_t){
        sizeof(hk_input_info_t), HK_INPUT_INFO_VERSION,
        HK_INPUT_BUTTON_ALL, HK_INPUT_SAMPLE_INTERVAL_US,
        HK_INPUT_DEBOUNCE_INTERVAL_US, HK_INPUT_EVENT_CAPACITY,
#if HK_BOARD_INPUT_PROFILE_CUBE
        HK_INPUT_BUTTON_BACK,
#else
        0U,
#endif
    };
    return HK_OK;
}

static hk_result_t k210_input_get_state(void *context, uint32_t *state)
{
    k210_input_state_t *input = (k210_input_state_t *)context;
    hk_result_t result = k210_input_service(input);

    if(result != HK_OK)
        return result;
    return hk_input_state_get(&input->state, state);
}

static hk_result_t k210_input_next_event(
    void *context, hk_input_cursor_t *cursor, hk_input_event_t *event)
{
    k210_input_state_t *input = (k210_input_state_t *)context;
    hk_result_t result = k210_input_service(input);

    if(result != HK_OK)
        return result;
    return hk_input_state_next_event(&input->state, cursor, event);
}

const hk_input_t hk_input_binding = {
    .context = &s_input,
    .open_cursor = k210_input_open_cursor,
    .get_info = k210_input_get_info,
    .get_state = k210_input_get_state,
    .next_event = k210_input_next_event,
};
