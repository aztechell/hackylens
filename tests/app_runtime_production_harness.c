#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <hackylens/capability/external_link.h>
#include <hackylens/capability/lights.h>

#include "../firmware/src/runtime/app_runtime_integration.h"
#include "../firmware/src/runtime/hk_main.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("FAIL line=%d: %s\n", __LINE__, #condition);             \
            return 1;                                                        \
        }                                                                    \
    } while(0)

typedef struct
{
    uint64_t now_us;
    hk_owner_t app_owner;
    hk_app_wakeup_token_t wakeup;
    hk_app_event_kind_t events[16];
    uint64_t sequences[16];
    uint8_t event_count;
    uint8_t input_sent;
    uint8_t wakeup_sent;
    uint8_t batch_active;
    uint32_t owner_initialize_count;
    uint32_t owner_enter_count;
    uint32_t owner_close_count;
    uint32_t owner_exit_count;
    uint32_t input_acquire_count;
    uint32_t input_state_count;
    uint32_t input_event_count;
    uint32_t render_count;
    uint32_t tick_count;
    uint32_t stop_count;
    uint32_t display_begin_count;
    uint32_t display_dirty_count;
    uint32_t display_clear_count;
    uint32_t display_present_count;
    uint32_t display_abort_count;
    uint32_t sleep_count;
    uint32_t activity_count;
    uint32_t shell_input_count;
    uint32_t menu_failure_count;
    uint32_t legacy_enter_count;
    uint32_t legacy_exit_count;
    hk_app_stop_reason_t stop_reason;
} fixture_t;

static fixture_t s_fixture;
static jmp_buf s_main_exit;
static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state[64];

static const char *const s_time_features[] = {"monotonic-us"};
static const char *const s_input_features[] = {"events"};
static const char *const s_display_features[] = {"base-plane"};
static const hk_app_capability_request_t s_capabilities[] = {
    {
        "hackylens.cap.time", 0U, ">=0.1.0", "<0.2.0",
        s_time_features, 1U, NULL, 0U,
    },
    {
        "hackylens.cap.input", 0U, ">=0.1.0", "<0.2.0",
        s_input_features, 1U, NULL, 0U,
    },
    {
        "hackylens.cap.display", 0U, ">=0.1.0", "<0.2.0",
        s_display_features, 1U, NULL, 0U,
    },
};

static hk_result_t app_start(const hk_app_context_t *ctx)
{
    hk_display_t display;
    hk_input_t input;
    hk_time_t time;

    if(hk_app_context_display(ctx, 0U, &display) != HK_OK ||
       hk_app_context_input(ctx, 0U, &input) != HK_OK ||
       hk_app_context_time(ctx, 0U, &time) != HK_OK)
        return HK_ERR_INTERNAL;
    return HK_OK;
}

static hk_result_t app_event(
    const hk_app_context_t *ctx,
    const hk_app_event_t *event)
{
    if(s_fixture.event_count >= 16U || event->sequence == 0U)
        return HK_ERR_LIMIT;
    s_fixture.events[s_fixture.event_count] = event->kind;
    s_fixture.sequences[s_fixture.event_count++] = event->sequence;
    if(event->kind == HK_APP_EVENT_INPUT)
        return hk_app_context_wakeup_token(ctx, 91U, &s_fixture.wakeup);
    if(event->kind == HK_APP_EVENT_TIMER)
        s_fixture.tick_count++;
    if(event->kind == HK_APP_EVENT_RUNTIME_CLOSE)
        s_fixture.stop_reason = event->data.close.reason;
    return HK_OK;
}

static hk_result_t app_render(
    const hk_app_context_t *ctx,
    hk_app_surface_t *surface)
{
    s_fixture.render_count++;
    if(hk_app_surface_clear(surface, 0U) != HK_OK)
        return HK_ERR_INTERNAL;
    if(s_fixture.render_count == 1U &&
       hk_app_context_request_render(ctx, NULL) != HK_OK)
        return HK_ERR_INTERNAL;
    return HK_OK;
}

static hk_result_t app_stop(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline;

    s_fixture.stop_count++;
    return hk_app_context_teardown_deadline(ctx, &deadline);
}

static const hk_app_v2_entry_t s_v2_entry = {
    .state_storage = s_state,
    .state_capacity_bytes = sizeof(s_state),
    .start = app_start,
    .event = app_event,
    .render = app_render,
    .stop = app_stop,
};

static const hk_app_t s_v2_app = {
    .struct_size = sizeof(hk_app_t),
    .struct_version = HK_APP_DESCRIPTOR_VERSION,
    .id = "production-host-v2",
    .title = "Production host v2",
    .lifecycle = HK_APP_LIFECYCLE_V2,
    .entry.v2 = &s_v2_entry,
    .limits = {
        sizeof(s_state), 256U, sizeof(s_state), HK_APP_STATE_ALIGNMENT,
        100U, 50U, 50U,
    },
    .capabilities = s_capabilities,
    .capability_count =
        (uint16_t)(sizeof(s_capabilities) / sizeof(s_capabilities[0])),
    .screen = SCREEN_APP_SLOT_0,
};

static void legacy_enter(const hk_input_snapshot_t *input)
{
    (void)input;
    s_fixture.legacy_enter_count++;
}

static void legacy_exit(void)
{
    s_fixture.legacy_exit_count++;
}

static const hk_legacy_app_entry_t s_legacy_entry = {
    .screen = SCREEN_BUTTONS,
    .enter = legacy_enter,
    .exit = legacy_exit,
};

static const hk_app_t s_legacy_app = {
    .struct_size = sizeof(hk_app_t),
    .struct_version = HK_APP_DESCRIPTOR_VERSION,
    .id = "production-host-legacy",
    .title = "Production host legacy",
    .lifecycle = HK_APP_LIFECYCLE_LEGACY,
    .entry.legacy = &s_legacy_entry,
    .screen = SCREEN_BUTTONS,
};

static hk_lease_t lease_for(hk_owner_t owner, hk_capability_id_t id)
{
    return (hk_lease_t){id, 1U, owner, id};
}

hk_result_t hk_generated_capability_request_for(
    const char *consumer_id,
    const char *capability_id,
    uint16_t instance,
    hk_capability_request_t *request)
{
    if(!consumer_id || strcmp(consumer_id, s_v2_app.id) != 0 ||
       !capability_id || instance != 0U || !request)
        return HK_ERR_NOT_DECLARED;
    if(strcmp(capability_id, "hackylens.cap.time") == 0)
        *request = (hk_capability_request_t)HK_TIME_REQUEST_0_1_INIT;
    else if(strcmp(capability_id, "hackylens.cap.input") == 0)
        *request = (hk_capability_request_t)HK_INPUT_REQUEST_0_1_INIT;
    else if(strcmp(capability_id, "hackylens.cap.display") == 0)
        *request = (hk_capability_request_t)HK_DISPLAY_REQUEST_0_1_INIT;
    else
        return HK_ERR_NOT_DECLARED;
    return HK_OK;
}

hk_result_t capability_owner_runtime_initialize(void)
{
    s_fixture.owner_initialize_count++;
    return HK_OK;
}

hk_result_t capability_owner_runtime_enter(const hk_app_t *app)
{
    if(!app)
        return HK_ERR_INVALID_ARGUMENT;
    s_fixture.owner_enter_count++;
    s_fixture.app_owner = (hk_owner_t){7U, s_fixture.owner_enter_count};
    return HK_OK;
}

hk_owner_t capability_owner_runtime_current(const hk_app_t *app)
{
    return app ? s_fixture.app_owner : HK_OWNER_NONE;
}

hk_result_t capability_owner_runtime_close(
    hk_owner_t owner,
    hk_deadline_t deadline)
{
    if(hk_owner_is_zero(owner) || owner.slot != s_fixture.app_owner.slot ||
       deadline.at_us == UINT64_MAX)
        return HK_ERR_WRONG_OWNER;
    s_fixture.owner_close_count++;
    s_fixture.app_owner = HK_OWNER_NONE;
    return HK_OK;
}

hk_result_t capability_owner_runtime_exit(const hk_app_t *app)
{
    if(!app || hk_owner_is_zero(s_fixture.app_owner))
        return HK_ERR_INVALID_STATE;
    s_fixture.owner_exit_count++;
    s_fixture.app_owner = HK_OWNER_NONE;
    return HK_OK;
}

hk_owner_t capability_client_consumer_owner(const char *consumer_id)
{
    if(consumer_id && strcmp(consumer_id, "consumer:firmware-runtime") == 0)
        return (hk_owner_t){90U, 1U};
    return HK_OWNER_NONE;
}

hk_result_t hk_time_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_time_t *handle)
{
    if(hk_owner_is_zero(owner) || !request || !handle ||
       request->id != HK_CAPABILITY_ID_TIME)
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = lease_for(owner, HK_CAPABILITY_ID_TIME);
    return HK_OK;
}

hk_result_t hk_time_now_us(
    hk_owner_t owner,
    const hk_time_t *handle,
    uint64_t *value)
{
    if(hk_owner_is_zero(owner) || !handle || !value ||
       handle->lease.capability_id != HK_CAPABILITY_ID_TIME)
        return HK_ERR_INVALID_ARGUMENT;
    *value = s_fixture.now_us;
    return HK_OK;
}

hk_result_t hk_time_deadline_after_us(
    hk_owner_t owner,
    const hk_time_t *handle,
    uint64_t duration_us,
    hk_deadline_t *deadline)
{
    if(hk_owner_is_zero(owner) || !handle || !deadline || duration_us == 0U ||
       handle->lease.capability_id != HK_CAPABILITY_ID_TIME ||
       s_fixture.now_us > UINT64_MAX - duration_us)
        return HK_ERR_INVALID_ARGUMENT;
    deadline->at_us = s_fixture.now_us + duration_us;
    return HK_OK;
}

hk_result_t hk_input_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_input_t *handle)
{
    if(hk_owner_is_zero(owner) || !request || !handle ||
       request->id != HK_CAPABILITY_ID_INPUT)
        return HK_ERR_INVALID_ARGUMENT;
    s_fixture.input_acquire_count++;
    handle->lease = lease_for(owner, HK_CAPABILITY_ID_INPUT);
    return HK_OK;
}

hk_result_t hk_input_get_state(
    hk_owner_t owner,
    const hk_input_t *handle,
    uint32_t *state)
{
    if(hk_owner_is_zero(owner) || !handle || !state ||
       handle->lease.capability_id != HK_CAPABILITY_ID_INPUT)
        return HK_ERR_INVALID_ARGUMENT;
    s_fixture.input_state_count++;
    *state = s_fixture.input_sent ? HK_INPUT_BUTTON_OK : 0U;
    return HK_OK;
}

hk_result_t hk_input_next_event(
    hk_owner_t owner,
    const hk_input_t *handle,
    hk_input_event_t *event)
{
    if(hk_owner_is_zero(owner) || !handle || !event ||
       handle->lease.capability_id != HK_CAPABILITY_ID_INPUT)
        return HK_ERR_INVALID_ARGUMENT;
    if(s_fixture.input_sent)
        return HK_PENDING;
    s_fixture.input_sent = 1U;
    s_fixture.input_event_count++;
    *event = (hk_input_event_t){
        1U, s_fixture.now_us, HK_INPUT_BUTTON_OK,
        HK_INPUT_BUTTON_OK, HK_INPUT_BUTTON_OK, 0U, 0U,
    };
    return HK_OK;
}

hk_result_t hk_display_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    uint32_t plane,
    hk_display_t *handle)
{
    if(hk_owner_is_zero(owner) || !request || !handle ||
       request->id != HK_CAPABILITY_ID_DISPLAY ||
       plane != HK_DISPLAY_PLANE_BASE)
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = lease_for(owner, HK_CAPABILITY_ID_DISPLAY);
    return HK_OK;
}

hk_result_t hk_ui_display_bind(hk_owner_t owner, const hk_display_t *display)
{
    if(hk_owner_is_zero(owner) || !display)
        return HK_ERR_INVALID_ARGUMENT;
    return HK_OK;
}

hk_result_t hk_display_get_info(
    hk_owner_t owner,
    const hk_display_t *handle,
    hk_display_info_t *info)
{
    if(hk_owner_is_zero(owner) || !handle || !info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_display_info_t){
        sizeof(*info), HK_DISPLAY_INFO_VERSION, 320U, 240U,
        HK_DISPLAY_FORMAT_RGB565_BE, HK_DISPLAY_PLANE_BASE,
        4U, 4U, 16U, 64U, 8U, 0U, 1024U, 20000U, 0U,
    };
    return HK_OK;
}

hk_result_t hk_display_begin_batch(
    hk_owner_t owner,
    const hk_display_t *handle)
{
    if(hk_owner_is_zero(owner) || !handle || s_fixture.batch_active)
        return HK_ERR_INVALID_STATE;
    s_fixture.batch_active = 1U;
    s_fixture.display_begin_count++;
    return HK_OK;
}

hk_result_t hk_display_mark_dirty(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *rect)
{
    if(hk_owner_is_zero(owner) || !handle || !rect ||
       !s_fixture.batch_active)
        return HK_ERR_INVALID_STATE;
    s_fixture.display_dirty_count++;
    return HK_OK;
}

hk_result_t hk_display_clear(
    hk_owner_t owner,
    const hk_display_t *handle,
    uint16_t rgb565)
{
    (void)rgb565;
    if(hk_owner_is_zero(owner) || !handle || !s_fixture.batch_active)
        return HK_ERR_INVALID_STATE;
    s_fixture.display_clear_count++;
    return HK_OK;
}

hk_result_t hk_display_fill_rect(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    (void)owner;
    (void)handle;
    (void)rect;
    (void)rgb565;
    return HK_OK;
}

hk_result_t hk_display_stroke_rect(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    return hk_display_fill_rect(owner, handle, rect, rgb565);
}

hk_result_t hk_display_text(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565)
{
    (void)owner;
    (void)handle;
    (void)bounds;
    (void)utf8;
    (void)size_bytes;
    (void)rgb565;
    return HK_OK;
}

hk_result_t hk_display_blit(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    (void)owner;
    (void)handle;
    (void)destination;
    (void)pixels;
    (void)pixel_format;
    return HK_OK;
}

hk_result_t hk_display_present(
    hk_owner_t owner,
    const hk_display_t *handle,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    (void)cancel;
    if(hk_owner_is_zero(owner) || !handle || !s_fixture.batch_active ||
       deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_STATE;
    s_fixture.batch_active = 0U;
    s_fixture.display_present_count++;
    return HK_OK;
}

hk_result_t hk_display_abort(
    hk_owner_t owner,
    const hk_display_t *handle)
{
    if(hk_owner_is_zero(owner) || !handle)
        return HK_ERR_INVALID_ARGUMENT;
    if(s_fixture.batch_active)
    {
        s_fixture.batch_active = 0U;
        s_fixture.display_abort_count++;
    }
    return HK_OK;
}

hk_result_t hk_display_surface_acquire(
    hk_owner_t owner,
    const hk_display_t *handle,
    hk_display_surface_t *surface)
{
    static uint8_t pixels[8];

    if(hk_owner_is_zero(owner) || !handle || !surface)
        return HK_ERR_INVALID_ARGUMENT;
    if(s_fixture.batch_active)
    {
        s_fixture.batch_active = 0U;
        s_fixture.display_abort_count++;
    }
    *surface = (hk_display_surface_t){
        sizeof(*surface), HK_DISPLAY_SURFACE_VERSION,
        {
            pixels, sizeof(pixels), 2U,
            HK_BUFFER_ACCESS_READABLE | HK_BUFFER_ACCESS_WRITABLE,
        },
        1U, 1U, HK_DISPLAY_FORMAT_RGB565_BE, 0U,
    };
    return HK_OK;
}

hk_result_t hk_lights_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    uint32_t channels,
    hk_lights_t *handle)
{
    (void)owner;
    (void)request;
    (void)channels;
    (void)handle;
    return HK_ERR_NOT_DECLARED;
}

hk_result_t hk_external_link_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    uint64_t mode_features,
    hk_external_link_t *handle)
{
    (void)owner;
    (void)request;
    (void)mode_features;
    (void)handle;
    return HK_ERR_NOT_DECLARED;
}

screen_t hk_screen_get(void)
{
    return SCREEN_APP_SLOT_0;
}

void activity_note(void)
{
    s_fixture.activity_count++;
}

void shell_show_menu_reason(hk_app_stop_reason_t reason)
{
    (void)reason;
    s_fixture.menu_failure_count++;
}

void shell_handle_buttons(const hk_input_snapshot_t *input)
{
    (void)input;
    s_fixture.shell_input_count++;
}

void menu_tick(const hk_input_snapshot_t *input)
{
    (void)input;
}

const hk_app_t *hk_app_for_screen(screen_t screen)
{
    return screen == SCREEN_APP_SLOT_0 ? &s_v2_app : NULL;
}

uint64_t hal_time_us(void)
{
    return s_fixture.now_us;
}

void hal_sleep_ms(uint32_t milliseconds)
{
    s_fixture.sleep_count++;
    s_fixture.now_us += (uint64_t)milliseconds * UINT64_C(1000);
    if(s_fixture.sleep_count >= 2U)
        longjmp(s_main_exit, 1);
}

static void system_tick(const hk_input_snapshot_t *input)
{
    (void)input;
    if(!s_fixture.wakeup_sent && s_fixture.wakeup.slot != 0U)
    {
        if(app_runtime_integration_wakeup(s_fixture.wakeup) != HK_OK)
            longjmp(s_main_exit, 2);
        s_fixture.wakeup_sent = 1U;
    }
}

static void startup(void)
{
    if(app_runtime_integration_initialize() != HK_OK ||
       app_runtime_integration_open(&s_v2_app, NULL) != HK_OK ||
       app_runtime_integration_media(HK_APP_MEDIA_MOUNTED, 1U) != HK_OK)
        longjmp(s_main_exit, 2);
}

int main(void)
{
    hk_main_hooks_t hooks = {
        .startup = startup,
        .system_tick = system_tick,
    };
    hk_input_snapshot_t snapshot = {0};
    int jump_result;

    memset(&s_fixture, 0, sizeof(s_fixture));
    s_fixture.now_us = 1000U;
    hk_main_set_hooks(&hooks);
    jump_result = setjmp(s_main_exit);
    if(jump_result == 0)
        (void)hk_main();
    CHECK(jump_result == 1);
    CHECK(app_runtime_integration_active() == &s_v2_app);
    CHECK(s_fixture.owner_initialize_count == 1U);
    CHECK(s_fixture.owner_enter_count == 1U);
    CHECK(s_fixture.input_acquire_count == 2U);
    CHECK(s_fixture.input_state_count >= 2U);
    CHECK(s_fixture.input_event_count == 1U);
    CHECK(s_fixture.activity_count == 1U);
    CHECK(s_fixture.shell_input_count == 0U);
    CHECK(s_fixture.menu_failure_count == 0U);
    CHECK(s_fixture.wakeup_sent == 1U);
    CHECK(s_fixture.tick_count == 1U);
    CHECK(s_fixture.render_count == 2U);
    CHECK(s_fixture.display_begin_count == 2U);
    CHECK(s_fixture.display_dirty_count == 2U);
    CHECK(s_fixture.display_clear_count == 2U);
    CHECK(s_fixture.display_present_count == 2U);
    CHECK(s_fixture.display_abort_count == 0U);
    CHECK(!s_fixture.batch_active);
    CHECK(s_fixture.events[0] == HK_APP_EVENT_MEDIA);
    CHECK(s_fixture.events[1] == HK_APP_EVENT_INPUT);
    CHECK(s_fixture.events[2] == HK_APP_EVENT_WAKEUP);
    CHECK(s_fixture.events[3] == HK_APP_EVENT_TIMER);
    CHECK(app_runtime_integration_close(HK_APP_STOP_BACK) == HK_OK);
    CHECK(s_fixture.events[4] == HK_APP_EVENT_RUNTIME_CLOSE);
    CHECK(s_fixture.event_count == 5U);
    for(uint8_t index = 0U; index < s_fixture.event_count; index++)
        CHECK(s_fixture.sequences[index] == (uint64_t)index + 1U);
    CHECK(s_fixture.stop_count == 1U);
    CHECK(s_fixture.owner_close_count == 1U);
    CHECK(s_fixture.stop_reason == HK_APP_STOP_BACK);
    CHECK(app_runtime_integration_active() == NULL);

    CHECK(app_runtime_integration_open(&s_legacy_app, &snapshot) == HK_OK);
    CHECK(s_fixture.legacy_enter_count == 1U);
    CHECK(app_runtime_integration_close(HK_APP_STOP_SWITCH) == HK_OK);
    CHECK(s_fixture.legacy_exit_count == 1U);
    CHECK(s_fixture.owner_exit_count == 1U);
    CHECK(s_fixture.owner_enter_count == 2U);
    CHECK(app_runtime_integration_active() == NULL);
    printf("APP_RUNTIME_PRODUCTION_OK\n");
    return 0;
}
