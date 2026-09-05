#include "qr_camera_app.h"

#include <stddef.h>

#include "qr_camera_controller.h"
#include "qr_camera_firmware.h"
#include "qr_service.h"

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state_storage[1024];
static void (*s_tick[1])(const hk_input_snapshot_t *);

_Static_assert(
    sizeof(qr_camera_state_t) <= sizeof(s_state_storage),
    "QR Camera state must fit the v2 storage slot");

void qr_camera_tick(const hk_input_snapshot_t *input) { qr_camera_controller_tick(input); }

uint8_t qr_camera_handle_debug_command(const char *cmd)
{
    char line[640];
    const hk_app_t *qr_camera = NULL;
    uint8_t index;

    if(str_eq_ci(cmd, "HKQRINFO"))
    {
        qr_service_format_info(
            line, sizeof(line),
            qr_camera_controller_settings_active() ?
                "QR-SETTINGS" :
                (qr_camera_session_active() ?
                     "QR-CAMERA" : screen_label(hk_screen_get())));
        debug_console_write_text(line);
        return 1U;
    }
    if(str_eq_ci(cmd, "HKQRCAM") || str_eq_ci(cmd, "HKQR"))
    {
        activity_note();
        for(index = 0U; index < g_menu_item_count; index++)
        {
            const hk_app_t *app = g_menu_items[index];

            if(app && app->id && str_eq_ci(app->id, "qr-camera"))
            {
                qr_camera = app;
                break;
            }
        }
        if(!qr_camera)
            return 1U;
        if(hk_screen_get() != SCREEN_MENU)
            shell_show_menu();
        (void)shell_open_app(qr_camera, NULL);
        return 1U;
    }
    if(str_eq_ci(cmd, "HKQRDECODE"))
    {
        activity_note();
        if(!qr_camera_session_active())
            debug_console_write_text("HKQRDECODE ERR NOTQR\n");
        else
        {
            qr_service_decode_force();
            qr_service_format_info(
                line, sizeof(line),
                qr_camera_controller_settings_active() ?
                    "QR-SETTINGS" : "QR-CAMERA");
            debug_console_write_text(line);
        }
        return 1U;
    }
    return 0U;
}

uint8_t qr_debug_handle_command(const char *cmd)
{
    static uint8_t (*s_debug_command[1])(const char *);

    if(s_debug_command[0] == NULL)
        s_debug_command[0] = qr_camera_handle_debug_command;
    if(s_debug_command[0] == NULL)
        return 0U;
    return s_debug_command[0](cmd);
}

static hk_result_t qr_camera_state_from(
    const hk_app_context_t *ctx, qr_camera_state_t **state)
{
    void *storage = NULL;
    uint32_t size_bytes = 0U;
    hk_result_t result;

    if(!state)
        return HK_ERR_INVALID_ARGUMENT;
    result = hk_app_context_state(ctx, &storage, &size_bytes);
    if(result != HK_OK)
        return result;
    if(!storage || size_bytes < sizeof(qr_camera_state_t))
        return HK_ERR_LIMIT;
    *state = (qr_camera_state_t *)storage;
    return HK_OK;
}

static hk_result_t qr_camera_finish_work(
    const hk_app_context_t *ctx, qr_camera_state_t *state)
{
    if(state->close_requested)
        return hk_app_context_request_close(ctx);
    return HK_OK;
}

static hk_result_t qr_camera_start(const hk_app_context_t *ctx)
{
    qr_camera_state_t *state = NULL;
    const char *app_id = NULL;
    uint32_t generation = 0U;
    hk_owner_t owner = HK_OWNER_NONE;
    hk_input_t input = {0};
    hk_result_t result = qr_camera_state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(hk_app_context_identity(ctx, &app_id, &generation, &owner) != HK_OK ||
       !app_id || generation == 0U || hk_owner_is_zero(owner) ||
       hk_app_context_input(ctx, 0U, &input) != HK_OK)
        return HK_ERR_INTERNAL;
    qr_camera_controller_reset(state);
    state->owner = owner;
    state->input = input;
    qr_camera_controller_enter(state);
    return HK_OK;
}

static hk_result_t qr_camera_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    qr_camera_state_t *state = NULL;
    uint32_t buttons = 0U;
    hk_input_snapshot_t input = {0};
    hk_result_t result = qr_camera_state_from(ctx, &state);

    if(result != HK_OK)
        return result;
    if(!event)
        return HK_ERR_INVALID_ARGUMENT;
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        qr_camera_controller_handle_input(state, &event->data.input);
        return qr_camera_finish_work(ctx, state);
    }
    if(event->kind == HK_APP_EVENT_TIMER)
    {
        if(hk_input_get_state(state->owner, &state->input, &buttons) != HK_OK)
            return HK_ERR_INTERNAL;
        input.state = buttons;
        if(s_tick[0] == NULL)
            s_tick[0] = qr_camera_tick;
        if(s_tick[0] != NULL)
            s_tick[0](&input);
        return qr_camera_finish_work(ctx, state);
    }
    return HK_OK;
}

static hk_result_t qr_camera_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    (void)ctx;
    (void)surface;
    return HK_OK;
}

static hk_result_t qr_camera_stop(const hk_app_context_t *ctx)
{
    qr_camera_state_t *state = NULL;
    hk_deadline_t deadline;
    hk_result_t result = qr_camera_state_from(ctx, &state);

    if(result == HK_OK)
        qr_camera_controller_exit(state);
    return hk_app_context_teardown_deadline(ctx, &deadline);
}

const hk_app_v2_entry_t qr_camera_v2_entry = {
    .state_storage = s_state_storage,
    .state_capacity_bytes = sizeof(s_state_storage),
    .start = qr_camera_start,
    .event = qr_camera_event,
    .render = qr_camera_render,
    .stop = qr_camera_stop,
};
