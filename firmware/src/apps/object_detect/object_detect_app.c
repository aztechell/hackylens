#include "object_detect_app.h"

#include <stdio.h>
#include <string.h>

#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "../../services/debug_console_service.h"
#include "object_detect_controller.h"
#include "object_detect_detector.h"
#include "object_detect_settings.h"
#include "object_detect_view.h"

uint8_t object_detect_handle_debug_command(const char *cmd)
{
    char line[512];

    if(str_eq_ci(cmd, "HKOBJECT"))
    {
        if(hk_screen_get() != SCREEN_MENU)
            shell_show_menu();
        (void)shell_open_app_id("object-detect", NULL);
        debug_console_write_text("HKOBJECT OK\r\n");
        return 1U;
    }
    if(!str_eq_ci(cmd, "HKOBJECTINFO"))
        return 0U;
    object_detect_detector_format_info(line, sizeof(line));
    {
        const object_detect_preferences_t *preferences =
            object_detect_settings_preferences();
        size_t length = strlen(line);

        while(length &&
              (line[length - 1U] == '\r' || line[length - 1U] == '\n'))
            line[--length] = '\0';
        snprintf(line + length, sizeof(line) - length,
                 " fps=%u light=%s rgb=%u/%u/%u\r\n",
                 preferences->fps_enabled,
                 preferences->light_mode ? "RGB" : "LED",
                 preferences->rgb_red,
                 preferences->rgb_green,
                 preferences->rgb_blue);
    }
    debug_console_write_text(line);
    return 1U;
}

void object_detect_draw_icon(uint16_t x, uint16_t y,
                             uint16_t color, uint16_t bg)
{
    object_detect_view_draw_icon(x, y, color, bg);
}


static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state_storage[1024];

typedef struct { hk_owner_t owner; hk_input_t input; } object_detect_state_t;

static object_detect_state_t *app_state(const hk_app_context_t *ctx)
{
    void *state = NULL;
    uint32_t bytes = 0U;
    if(hk_app_context_state(ctx, &state, &bytes) != HK_OK || bytes < sizeof(object_detect_state_t))
        return NULL;
    return state;
}

static hk_result_t app_start(const hk_app_context_t *ctx)
{
    object_detect_state_t *state = app_state(ctx);
    const char *id;
    uint32_t generation;
    hk_input_snapshot_t input = {0};
    if(!state || hk_app_context_identity(ctx, &id, &generation, &state->owner) != HK_OK ||
       hk_app_context_input(ctx, 0U, &state->input) != HK_OK ||
       hk_input_get_state(state->owner, &state->input, &input.state) != HK_OK)
        return HK_ERR_INTERNAL;
    object_detect_controller_enter(&input);
    return HK_OK;
}

static hk_result_t app_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    object_detect_state_t *state = app_state(ctx);
    hk_input_snapshot_t input = {0};
    if(!state || !event)
        return HK_ERR_INVALID_ARGUMENT;
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        input.state = event->data.input.state;
        input.changed = event->data.input.changed;
        input.pressed = input.state & input.changed;
        if(object_detect_controller_handle_buttons(&input))
            return hk_app_context_request_close(ctx);
    }
    else if(event->kind == HK_APP_EVENT_TIMER)
    {
        if(hk_input_get_state(state->owner, &state->input, &input.state) != HK_OK)
            return HK_ERR_INTERNAL;
        object_detect_controller_tick(&input);
    }
    return HK_OK;
}

static hk_result_t app_stop(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline;
    hk_result_t result = hk_app_context_teardown_deadline(ctx, &deadline);
    if(result == HK_OK)
        object_detect_detector_limit_unload(deadline.at_us);
    object_detect_controller_exit();
    return result;
}

const hk_app_v2_entry_t object_detect_v2_entry = {
    .state_storage = s_state_storage,
    .state_capacity_bytes = sizeof(s_state_storage),
    .start = app_start,
    .event = app_event,
    .render = NULL,
    .stop = app_stop,
};
