#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/src/app_runtime/switch.h"

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
    hk_app_switch_t switcher;
    uint64_t now_us;
    uint64_t now_step_us;
    hk_app_event_kind_t events[16];
    uint64_t sequences[16];
    uint8_t event_count;
    uint8_t back_during_start;
    uint8_t back_input_during_start;
    uint8_t close_on_back;
    uint8_t start_fails;
    uint8_t deadline_fails;
    uint8_t render_fails;
    uint8_t present_fails;
    uint8_t begin_busy;
    uint8_t render_requests_again;
    uint8_t batch_active;
    uint32_t owner_open_count;
    uint32_t owner_cleanup_count;
    uint32_t stop_count;
    uint32_t tick_count;
    uint32_t render_count;
    uint32_t legacy_open_count;
    uint32_t legacy_enter_count;
    uint32_t legacy_exit_count;
    uint32_t legacy_close_count;
    uint32_t invalidate_count;
    uint32_t present_count;
    uint32_t abort_count;
    hk_app_stop_reason_t stop_reason;
    hk_app_wakeup_token_t token;
    hk_app_surface_t *borrowed_surface;
} fixture_t;

static fixture_t *s_fixture;
static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state[64];

static hk_result_t app_start(const hk_app_context_t *ctx)
{
    (void)ctx;
    if(s_fixture->back_during_start)
    {
        if(hk_app_switch_close(
               &s_fixture->switcher, HK_APP_STOP_BACK) != HK_PENDING)
            return HK_ERR_INTERNAL;
    }
    if(s_fixture->back_input_during_start)
    {
        uint8_t consumed = 0U;
        hk_input_event_t input = {0};

        input.changed = HK_INPUT_BUTTON_BACK;
        input.pressed = HK_INPUT_BUTTON_BACK;
        input.state = HK_INPUT_BUTTON_BACK;
        if(hk_app_switch_input(&s_fixture->switcher, &input, &consumed) != HK_OK ||
           !consumed)
            return HK_ERR_INTERNAL;
    }
    return s_fixture->start_fails ? HK_ERR_IO : HK_OK;
}

static hk_result_t app_event(
    const hk_app_context_t *ctx,
    const hk_app_event_t *event)
{
    if(s_fixture->event_count >= 16U || event->sequence == 0U)
        return HK_ERR_LIMIT;
    s_fixture->events[s_fixture->event_count] = event->kind;
    s_fixture->sequences[s_fixture->event_count++] = event->sequence;
    if(event->kind == HK_APP_EVENT_INPUT)
        return hk_app_context_wakeup_token(ctx, 77U, &s_fixture->token);
    if(event->kind == HK_APP_EVENT_TIMER)
    {
        static const hk_display_rect_t region = {1, 2, 3U, 4U};

        s_fixture->tick_count++;
        return hk_app_context_request_render(ctx, &region);
    }
    if(event->kind == HK_APP_EVENT_RUNTIME_CLOSE)
        s_fixture->stop_reason = event->data.close.reason;
    if(event->kind == HK_APP_EVENT_INPUT && s_fixture->close_on_back &&
       (event->data.input.pressed & HK_INPUT_BUTTON_BACK))
        return hk_app_context_request_close(ctx);
    return HK_OK;
}

static hk_result_t app_render(
    const hk_app_context_t *ctx,
    hk_app_surface_t *surface)
{
    hk_display_info_t info = {0};

    s_fixture->render_count++;
    s_fixture->borrowed_surface = surface;
    if(hk_app_surface_get_info(surface, &info) != HK_OK ||
       info.width != 320U || info.height != 240U)
        return HK_ERR_INTERNAL;
    if(hk_app_surface_clear(surface, 0U) != HK_OK)
        return HK_ERR_INTERNAL;
    if(s_fixture->render_requests_again && s_fixture->render_count == 1U &&
       hk_app_context_request_render(ctx, NULL) != HK_OK)
        return HK_ERR_INTERNAL;
    return s_fixture->render_fails ? HK_ERR_IO : HK_OK;
}

static hk_result_t app_stop(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline;

    s_fixture->stop_count++;
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

static void legacy_enter(const hk_input_snapshot_t *input)
{
    (void)input;
    s_fixture->legacy_enter_count++;
}

static void legacy_exit(void)
{
    s_fixture->legacy_exit_count++;
}

static const hk_legacy_app_entry_t s_legacy_entry = {
    .screen = SCREEN_BUTTONS,
    .enter = legacy_enter,
    .exit = legacy_exit,
};

static hk_app_t v2_descriptor(void)
{
    hk_app_t app = {0};

    app.struct_size = sizeof(app);
    app.struct_version = HK_APP_DESCRIPTOR_VERSION;
    app.id = "mixed-v2";
    app.title = "Mixed v2";
    app.lifecycle = HK_APP_LIFECYCLE_V2;
    app.entry.v2 = &s_v2_entry;
    app.limits.static_ram_bytes = sizeof(s_state);
    app.limits.stack_bytes = 256U;
    app.limits.state_bytes = sizeof(s_state);
    app.limits.state_alignment = HK_APP_STATE_ALIGNMENT;
    app.limits.tick_interval_us = 100U;
    app.limits.tick_budget_us = 10U;
    app.limits.render_budget_us = 20U;
    return app;
}

static hk_app_t legacy_descriptor(void)
{
    hk_app_t app = {0};

    app.struct_size = sizeof(app);
    app.struct_version = HK_APP_DESCRIPTOR_VERSION;
    app.id = "mixed-legacy";
    app.title = "Mixed legacy";
    app.lifecycle = HK_APP_LIFECYCLE_LEGACY;
    app.entry.legacy = &s_legacy_entry;
    app.screen = SCREEN_BUTTONS;
    return app;
}

static hk_result_t resolve_capability(
    void *user,
    const hk_app_t *app,
    const hk_app_capability_request_t *declaration,
    hk_capability_request_t *request)
{
    (void)user;
    (void)app;
    (void)declaration;
    (void)request;
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t resolve_service(
    void *user,
    const hk_app_t *app,
    const hk_app_service_request_t *declaration)
{
    (void)user;
    (void)app;
    (void)declaration;
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t owner_open(
    void *user,
    const hk_app_t *app,
    hk_owner_t *owner)
{
    fixture_t *fixture = user;

    (void)app;
    fixture->owner_open_count++;
    *owner = (hk_owner_t){2U, fixture->owner_open_count};
    return HK_OK;
}

static hk_result_t acquire_capability(
    void *user,
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_lease_t *lease)
{
    (void)user;
    (void)owner;
    (void)request;
    (void)lease;
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t acquire_service(
    void *user,
    hk_owner_t owner,
    const hk_app_service_request_t *declaration)
{
    (void)user;
    (void)owner;
    (void)declaration;
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t owner_cleanup(
    void *user,
    hk_owner_t owner,
    hk_deadline_t deadline)
{
    fixture_t *fixture = user;

    if(hk_owner_is_zero(owner) || deadline.at_us == UINT64_MAX)
        return HK_ERR_INTERNAL;
    fixture->owner_cleanup_count++;
    return HK_OK;
}

static hk_result_t deadline_after_us(
    void *user,
    uint64_t duration_us,
    hk_deadline_t *deadline)
{
    fixture_t *fixture = user;

    if(fixture->deadline_fails)
        return HK_ERR_IO;
    if(duration_us == 0U || fixture->now_us > UINT64_MAX - duration_us)
        return HK_ERR_LIMIT;
    deadline->at_us = fixture->now_us + duration_us;
    return HK_OK;
}

static hk_result_t fake_now(void *user, uint64_t *now_us)
{
    fixture_t *fixture = user;

    *now_us = fixture->now_us;
    fixture->now_us += fixture->now_step_us;
    return HK_OK;
}

static hk_result_t fake_legacy_open(void *user, const hk_app_t *app)
{
    fixture_t *fixture = user;

    (void)app;
    fixture->legacy_open_count++;
    return HK_OK;
}

static hk_result_t fake_legacy_close(void *user, const hk_app_t *app)
{
    fixture_t *fixture = user;

    (void)app;
    fixture->legacy_close_count++;
    return HK_OK;
}

static hk_result_t surface_invalidate(
    void *user,
    const hk_display_rect_t *region)
{
    fixture_t *fixture = user;

    (void)region;
    fixture->invalidate_count++;
    return HK_OK;
}

static hk_result_t surface_clear(void *user, uint16_t rgb565)
{
    (void)user;
    (void)rgb565;
    return HK_OK;
}

static hk_result_t surface_rect(
    void *user,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    (void)user;
    (void)rect;
    (void)rgb565;
    return HK_OK;
}

static hk_result_t surface_text(
    void *user,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565)
{
    (void)user;
    (void)bounds;
    (void)utf8;
    (void)size_bytes;
    (void)rgb565;
    return HK_OK;
}

static hk_result_t surface_blit(
    void *user,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    (void)user;
    (void)destination;
    (void)pixels;
    (void)pixel_format;
    return HK_OK;
}

static hk_result_t render_begin(
    void *user,
    const hk_app_runtime_t *runtime,
    hk_app_surface_t *surface)
{
    static const hk_display_info_t info = {
        sizeof(hk_display_info_t), HK_DISPLAY_INFO_VERSION,
        320U, 240U, HK_DISPLAY_FORMAT_RGB565_BE, HK_DISPLAY_PLANE_BASE,
        4U, 4U, 16U, 64U, 8U, 0U, 1024U, 20000U, 0U,
    };
    fixture_t *fixture = user;
    hk_app_surface_ops_t ops = {
        .user = fixture,
        .invalidate = surface_invalidate,
        .clear = surface_clear,
        .fill_rect = surface_rect,
        .stroke_rect = surface_rect,
        .text = surface_text,
        .blit = surface_blit,
    };

    if(fixture->begin_busy)
        return HK_ERR_BUSY;
    fixture->batch_active = 1U;
    return hk_app_surface_private_init(
        surface, runtime->context_generation, &info, &ops);
}

static hk_result_t render_present(void *user, hk_deadline_t deadline)
{
    fixture_t *fixture = user;

    (void)deadline;
    fixture->present_count++;
    if(fixture->present_fails)
        return HK_ERR_IO;
    fixture->batch_active = 0U;
    return HK_OK;
}

static hk_result_t render_abort(void *user)
{
    fixture_t *fixture = user;

    if(fixture->batch_active)
    {
        fixture->abort_count++;
        fixture->batch_active = 0U;
    }
    return HK_OK;
}

static int reset_fixture(fixture_t *fixture)
{
    hk_app_runtime_ops_t runtime_ops = {
        .user = fixture,
        .resolve_capability = resolve_capability,
        .resolve_service = resolve_service,
        .owner_open = owner_open,
        .acquire_capability = acquire_capability,
        .acquire_service = acquire_service,
        .owner_cleanup = owner_cleanup,
        .deadline_after_us = deadline_after_us,
    };
    hk_app_switch_ops_t switch_ops = {
        .user = fixture,
        .legacy_open = fake_legacy_open,
        .legacy_close = fake_legacy_close,
        .now_us = fake_now,
        .render_begin = render_begin,
        .render_present = render_present,
        .render_abort = render_abort,
    };

    memset(fixture, 0, sizeof(*fixture));
    fixture->now_us = 1000U;
    s_fixture = fixture;
    CHECK(hk_app_switch_init(
        &fixture->switcher, &runtime_ops, &switch_ops, 5000U) == HK_OK);
    return 0;
}

static int check_mixed_switch_and_events(void)
{
    fixture_t fixture;
    hk_app_t legacy = legacy_descriptor();
    hk_app_t v2 = v2_descriptor();
    hk_input_snapshot_t snapshot = {0};
    hk_input_event_t input = {0};
    hk_display_info_t stale_info;
    hk_app_wakeup_token_t stale;
    uint8_t consumed = 0U;

    CHECK(reset_fixture(&fixture) == 0);
    CHECK(hk_app_switch_open(&fixture.switcher, &legacy, &snapshot) == HK_OK);
    CHECK(fixture.legacy_open_count == 1U && fixture.legacy_enter_count == 1U);
    CHECK(hk_app_switch_open(&fixture.switcher, &v2, &snapshot) == HK_OK);
    CHECK(fixture.legacy_exit_count == 1U && fixture.legacy_close_count == 1U);
    CHECK(hk_app_switch_active(&fixture.switcher) == &v2);

    CHECK(hk_app_switch_poll(&fixture.switcher, 1000U) == HK_OK);
    CHECK(fixture.render_count == 1U && fixture.present_count == 1U);
    CHECK(fixture.invalidate_count == 1U && !fixture.batch_active);
    CHECK(hk_app_surface_get_info(fixture.borrowed_surface, &stale_info) ==
          HK_ERR_STALE_HANDLE);

    input.sequence = 9U;
    input.timestamp_us = 1010U;
    input.state = HK_INPUT_BUTTON_OK;
    input.changed = HK_INPUT_BUTTON_OK;
    input.pressed = HK_INPUT_BUTTON_OK;
    CHECK(hk_app_switch_input(&fixture.switcher, &input, &consumed) == HK_OK);
    CHECK(consumed && fixture.events[0] == HK_APP_EVENT_INPUT);
    CHECK(fixture.token.value == 77U);
    stale = fixture.token;
    CHECK(hk_app_switch_media(
        &fixture.switcher, HK_APP_MEDIA_MOUNTED, 3U, 1020U) == HK_OK);
    CHECK(hk_app_switch_wakeup(&fixture.switcher, fixture.token, 1030U) == HK_OK);

    fixture.now_us = 1100U;
    CHECK(hk_app_switch_poll(&fixture.switcher, 1100U) == HK_OK);
    CHECK(fixture.tick_count == 1U && fixture.render_count == 2U);
    CHECK(fixture.events[1] == HK_APP_EVENT_MEDIA);
    CHECK(fixture.events[2] == HK_APP_EVENT_WAKEUP);
    CHECK(fixture.events[3] == HK_APP_EVENT_TIMER);

    input.state = 0U;
    input.changed = HK_INPUT_BUTTON_BACK;
    input.pressed = HK_INPUT_BUTTON_BACK;
    CHECK(hk_app_switch_input(&fixture.switcher, &input, &consumed) == HK_OK);
    CHECK(consumed && fixture.events[4] == HK_APP_EVENT_INPUT);
    CHECK(hk_app_switch_active(&fixture.switcher) == &v2);
    CHECK(hk_app_switch_close(&fixture.switcher, HK_APP_STOP_BACK) == HK_OK);
    CHECK(fixture.events[5] == HK_APP_EVENT_RUNTIME_CLOSE);
    for(uint8_t index = 0U; index < fixture.event_count; index++)
        CHECK(fixture.sequences[index] == (uint64_t)index + 1U);
    CHECK(fixture.stop_reason == HK_APP_STOP_BACK);
    CHECK(hk_app_switch_active(&fixture.switcher) == NULL);
    CHECK(hk_app_switch_wakeup(&fixture.switcher, stale, 1200U) ==
          HK_ERR_STALE_HANDLE);

    CHECK(hk_app_switch_open(&fixture.switcher, &v2, &snapshot) == HK_OK);
    CHECK(hk_app_switch_open(&fixture.switcher, &legacy, &snapshot) == HK_OK);
    CHECK(fixture.stop_reason == HK_APP_STOP_SWITCH);
    CHECK(hk_app_switch_open(&fixture.switcher, &v2, &snapshot) == HK_OK);
    CHECK(hk_app_switch_wakeup(&fixture.switcher, stale, 1300U) ==
          HK_ERR_STALE_HANDLE);
    CHECK(hk_app_switch_close(
        &fixture.switcher, HK_APP_STOP_COMPLETED) == HK_OK);
    return 0;
}

static int check_back_during_start(void)
{
    fixture_t fixture;
    hk_app_t v2 = v2_descriptor();

    CHECK(reset_fixture(&fixture) == 0);
    fixture.back_during_start = 1U;
    CHECK(hk_app_switch_open(&fixture.switcher, &v2, NULL) == HK_ERR_CANCELLED);
    CHECK(hk_app_switch_active(&fixture.switcher) == NULL);
    CHECK(fixture.stop_count == 1U);
    CHECK(fixture.owner_cleanup_count == 1U);
    CHECK(fixture.stop_reason == HK_APP_STOP_BACK);
    return 0;
}

static int check_back_input_during_start(void)
{
    fixture_t fixture;
    hk_app_t v2 = v2_descriptor();

    CHECK(reset_fixture(&fixture) == 0);
    fixture.back_input_during_start = 1U;
    CHECK(hk_app_switch_open(&fixture.switcher, &v2, NULL) == HK_OK);
    CHECK(hk_app_switch_active(&fixture.switcher) == &v2);
    CHECK(fixture.event_count == 1U);
    CHECK(fixture.events[0] == HK_APP_EVENT_INPUT);
    CHECK(fixture.stop_count == 0U);
    CHECK(hk_app_switch_close(&fixture.switcher, HK_APP_STOP_COMPLETED) == HK_OK);
    return 0;
}

static int check_timeout_and_render_failures(void)
{
    fixture_t fixture;
    hk_app_t v2 = v2_descriptor();

    CHECK(reset_fixture(&fixture) == 0);
    CHECK(hk_app_switch_open(&fixture.switcher, &v2, NULL) == HK_OK);
    fixture.now_us = 1100U;
    fixture.now_step_us = 11U;
    CHECK(hk_app_switch_poll(&fixture.switcher, 1100U) ==
          HK_ERR_DEADLINE_EXCEEDED);
    CHECK(fixture.stop_reason == HK_APP_STOP_DEADLINE);
    CHECK(hk_app_switch_active(&fixture.switcher) == NULL);

    CHECK(reset_fixture(&fixture) == 0);
    fixture.render_fails = 1U;
    CHECK(hk_app_switch_open(&fixture.switcher, &v2, NULL) == HK_OK);
    CHECK(hk_app_switch_poll(&fixture.switcher, 1000U) == HK_ERR_IO);
    CHECK(fixture.stop_reason == HK_APP_STOP_CALLBACK_FAILED);
    CHECK(fixture.abort_count == 1U && !fixture.batch_active);

    CHECK(reset_fixture(&fixture) == 0);
    fixture.deadline_fails = 1U;
    CHECK(hk_app_switch_open(&fixture.switcher, &v2, NULL) == HK_OK);
    CHECK(hk_app_switch_poll(&fixture.switcher, 1000U) == HK_ERR_IO);
    CHECK(fixture.stop_reason == HK_APP_STOP_DEADLINE);
    CHECK(fixture.abort_count == 1U && !fixture.batch_active);
    CHECK(hk_app_switch_active(&fixture.switcher) == NULL);

    CHECK(reset_fixture(&fixture) == 0);
    fixture.present_fails = 1U;
    CHECK(hk_app_switch_open(&fixture.switcher, &v2, NULL) == HK_OK);
    CHECK(hk_app_switch_poll(&fixture.switcher, 1000U) == HK_ERR_IO);
    CHECK(fixture.stop_reason == HK_APP_STOP_CALLBACK_FAILED);
    CHECK(fixture.abort_count == 1U && !fixture.batch_active);
    return 0;
}

static int check_busy_begin_skips_surface_present(void)
{
    fixture_t fixture;
    hk_app_t v2 = v2_descriptor();

    CHECK(reset_fixture(&fixture) == 0);
    fixture.begin_busy = 1U;
    CHECK(hk_app_switch_open(&fixture.switcher, &v2, NULL) == HK_OK);
    CHECK(hk_app_switch_poll(&fixture.switcher, 1000U) == HK_OK);
    CHECK(hk_app_switch_active(&fixture.switcher) == &v2);
    CHECK(fixture.render_count == 0U);
    CHECK(!hk_app_runtime_render_pending(&fixture.switcher.runtime));
    CHECK(hk_app_switch_close(
        &fixture.switcher, HK_APP_STOP_COMPLETED) == HK_OK);
    return 0;
}

static int check_render_requested_during_render_is_immediate(void)
{
    fixture_t fixture;
    hk_app_t v2 = v2_descriptor();

    CHECK(reset_fixture(&fixture) == 0);
    fixture.render_requests_again = 1U;
    CHECK(hk_app_switch_open(&fixture.switcher, &v2, NULL) == HK_OK);
    CHECK(hk_app_switch_poll_interval_us(&fixture.switcher, 1000U) == 1U);
    CHECK(hk_app_switch_poll(&fixture.switcher, 1000U) == HK_OK);
    CHECK(fixture.render_count == 1U);
    CHECK(hk_app_runtime_render_pending(&fixture.switcher.runtime));
    CHECK(hk_app_switch_poll_interval_us(&fixture.switcher, 1000U) == 1U);
    CHECK(hk_app_switch_poll(&fixture.switcher, 1001U) == HK_OK);
    CHECK(fixture.render_count == 2U);
    CHECK(!hk_app_runtime_render_pending(&fixture.switcher.runtime));
    CHECK(hk_app_switch_close(
        &fixture.switcher, HK_APP_STOP_COMPLETED) == HK_OK);
    return 0;
}

static int check_autostart_failure_fallback(void)
{
    fixture_t fixture;
    hk_app_t v2 = v2_descriptor();
    hk_app_t fallback = legacy_descriptor();

    CHECK(reset_fixture(&fixture) == 0);
    fixture.start_fails = 1U;
    CHECK(hk_app_switch_open(&fixture.switcher, &v2, NULL) == HK_ERR_IO);
    CHECK(hk_app_switch_active(&fixture.switcher) == NULL);
    fixture.start_fails = 0U;
    CHECK(hk_app_switch_open(&fixture.switcher, &fallback, NULL) == HK_OK);
    CHECK(hk_app_switch_active(&fixture.switcher) == &fallback);
    CHECK(fixture.legacy_enter_count == 1U);
    CHECK(hk_app_switch_close(&fixture.switcher, HK_APP_STOP_FORCED) == HK_OK);
    return 0;
}

int main(void)
{
    CHECK(check_mixed_switch_and_events() == 0);
    CHECK(check_back_during_start() == 0);
    CHECK(check_back_input_during_start() == 0);
    CHECK(check_timeout_and_render_failures() == 0);
    CHECK(check_busy_begin_skips_surface_present() == 0);
    CHECK(check_render_requested_during_render_is_immediate() == 0);
    CHECK(check_autostart_failure_fallback() == 0);
    printf("APP_RUNTIME_MIXED_OK\n");
    return 0;
}
