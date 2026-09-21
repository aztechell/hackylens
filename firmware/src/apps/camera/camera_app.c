#include "../../services/camera_light.h"
#include "camera_app.h"

#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "camera_controller.h"
#include "camera_view.h"

uint8_t camera_handle_debug_command(const char *cmd)
{
    if(!str_eq_ci(cmd, "HKCAMERA") && !str_eq_ci(cmd, "HKCAM"))
        return 0U;
    activity_note();
    if(hk_screen_get() != SCREEN_MENU)
        shell_show_menu();
    (void)shell_open_app_id("camera", NULL);
    return 1U;
}

void camera_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    camera_view_draw_icon(x, y, color, bg);
}


static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state_storage[1024];

typedef struct { const hk_input_t *input; } camera_state_t;

static camera_state_t *app_state(const hk_app_context_t *ctx)
{
    void *state = NULL;
    uint32_t bytes = 0U;
    if(hk_app_context_state(ctx, &state, &bytes) != HK_OK || bytes < sizeof(camera_state_t))
        return NULL;
    return state;
}

static hk_result_t app_start(const hk_app_context_t *ctx)
{
    camera_state_t *state = app_state(ctx);
    const char *id;
    uint32_t generation;
    hk_input_snapshot_t input = {0};
    if(!state || hk_app_context_identity(ctx, &id, &generation) != HK_OK ||
       hk_app_context_input(ctx, &state->input) != HK_OK ||
       hk_input_get_state(state->input, &input.state) != HK_OK)
        return HK_ERR_INTERNAL;
    camera_controller_enter(&input);
    return HK_OK;
}

static hk_result_t app_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    camera_state_t *state = app_state(ctx);
    hk_input_snapshot_t input = {0};
    if(!state || !event)
        return HK_ERR_INVALID_ARGUMENT;
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        input.state = event->data.input.state;
        input.changed = event->data.input.changed;
        input.pressed = input.state & input.changed;
        if(camera_controller_handle_input(&input))
            return hk_app_context_request_close(ctx);
    }
    else if(event->kind == HK_APP_EVENT_TIMER)
    {
        if(hk_input_get_state(state->input, &input.state) != HK_OK)
            return HK_ERR_INTERNAL;
        camera_controller_tick(&input);
    }
    return HK_OK;
}

static hk_result_t app_stop(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline;
    hk_result_t result = hk_app_context_teardown_deadline(ctx, &deadline);
    {
        hk_result_t light_result = camera_light_retire(
            result == HK_OK ? deadline : (hk_deadline_t){UINT64_MAX});
        if(result == HK_OK)
            result = light_result;
    }
    camera_controller_exit();
    return result;
}

const hk_app_v2_entry_t camera_v2_entry = {
    .state_storage = s_state_storage,
    .state_capacity_bytes = sizeof(s_state_storage),
    .start = app_start,
    .event = app_event,
    .render = NULL,
    .stop = app_stop,
};
