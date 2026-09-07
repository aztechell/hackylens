#include "face_detect_app.h"

#include <stddef.h>

#include "../../core/hk_string.h"
#include "../../services/debug_console_service.h"
#include "face_detect_controller.h"
#include "face_detect_detector.h"
#include "face_detect_view.h"

uint8_t face_detect_handle_debug_command(const char *cmd)
{
    char line[192];

    if(!str_eq_ci(cmd, "HKFACEINFO"))
        return 0;
    face_detect_detector_format_info(line, sizeof(line));
    debug_console_write_text(line);
    return 1;
}

void face_detect_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    face_detect_view_draw_icon(x, y, color, bg);
}


static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state_storage[1024];

typedef struct { const hk_input_t *input; } face_detect_state_t;

static face_detect_state_t *app_state(const hk_app_context_t *ctx)
{
    void *state = NULL;
    uint32_t bytes = 0U;
    if(hk_app_context_state(ctx, &state, &bytes) != HK_OK || bytes < sizeof(face_detect_state_t))
        return NULL;
    return state;
}

static hk_result_t app_start(const hk_app_context_t *ctx)
{
    face_detect_state_t *state = app_state(ctx);
    const char *id;
    uint32_t generation;
    hk_owner_t owner = HK_OWNER_NONE;
    hk_input_snapshot_t input = {0};
    if(!state || hk_app_context_identity(ctx, &id, &generation, &owner) != HK_OK ||
       hk_app_context_input(ctx, &state->input) != HK_OK ||
       hk_input_get_state(state->input, &input.state) != HK_OK)
        return HK_ERR_INTERNAL;
    face_detect_controller_enter(&input);
    return HK_OK;
}

static hk_result_t app_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    face_detect_state_t *state = app_state(ctx);
    hk_input_snapshot_t input = {0};
    if(!state || !event)
        return HK_ERR_INVALID_ARGUMENT;
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        input.state = event->data.input.state;
        input.changed = event->data.input.changed;
        input.pressed = input.state & input.changed;
        if(face_detect_controller_handle_buttons(&input))
            return hk_app_context_request_close(ctx);
    }
    else if(event->kind == HK_APP_EVENT_TIMER)
    {
        if(hk_input_get_state(state->input, &input.state) != HK_OK)
            return HK_ERR_INTERNAL;
        face_detect_controller_tick(&input);
    }
    return HK_OK;
}

static hk_result_t app_stop(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline;
    hk_result_t result = hk_app_context_teardown_deadline(ctx, &deadline);
    if(result == HK_OK)
        face_detect_detector_limit_unload(deadline.at_us);
    face_detect_controller_exit();
    return result;
}

const hk_app_v2_entry_t face_detect_v2_entry = {
    .state_storage = s_state_storage,
    .state_capacity_bytes = sizeof(s_state_storage),
    .start = app_start,
    .event = app_event,
    .render = NULL,
    .stop = app_stop,
};
