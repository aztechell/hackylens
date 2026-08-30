#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/src/app_runtime/runtime_private.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("FAIL line=%d: %s\n", __LINE__, #condition);             \
            return 1;                                                        \
        }                                                                    \
    } while(0)

typedef enum
{
    MODE_AVAILABLE = 0,
    MODE_REQUIRED_ABSENT,
    MODE_VERSION_MISMATCH,
    MODE_FEATURE_MISMATCH,
    MODE_OPTIONAL_ABSENT,
    MODE_PARTIAL_INJECTION,
    MODE_OWNER_EXHAUSTED,
} mode_t;

typedef struct
{
    hk_app_runtime_t *runtime;
    mode_t mode;
    uint32_t resolve_calls;
    uint32_t probe_calls;
    uint32_t owner_open_calls;
    uint32_t acquire_calls;
    uint32_t service_acquire_calls;
    uint32_t prepare_calls;
    uint32_t cleanup_calls;
    uint32_t owner_cleanup_calls;
    uint32_t next_owner_generation;
    uint8_t owner_live;
    hk_owner_t live_owner;
    hk_app_context_t copied_context;
    hk_time_t copied_time;
} grants_fixture_t;

static grants_fixture_t *s_fixture;
static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state[32];

static hk_result_t fake_direct_acquire(
    hk_owner_t owner,
    hk_capability_id_t id)
{
    if(!s_fixture->owner_live ||
       owner.slot != s_fixture->live_owner.slot ||
       owner.generation != s_fixture->live_owner.generation)
        return HK_ERR_STALE_HANDLE;
    if(id != HK_CAPABILITY_ID_TIME && id != HK_CAPABILITY_ID_INPUT)
        return HK_ERR_NOT_DECLARED;
    return HK_OK;
}

static const char *const s_time_features[] = {
    "monotonic-us",
    "sleep-until",
};
static const char *const s_input_features[] = {
    "state",
};
static const hk_app_capability_request_t s_capabilities[] = {
    {
        "hackylens.cap.time", 0U, "0.1.0", "0.2.0",
        s_time_features, 2U, NULL, 0U,
    },
    {
        "hackylens.cap.input", 0U, "0.1.0", "0.2.0",
        s_input_features, 1U, "headless", 1U,
    },
};
static const hk_app_service_request_t s_services[] = {
    {"hackylens.service.settings", "fixture.settings"},
};

static hk_result_t probe(hk_app_context_t *ctx)
{
    const char *app_id = NULL;
    const char *fallback = NULL;
    hk_owner_t owner = HK_OWNER_NONE;
    hk_time_t time = {0};
    uint32_t generation = 0U;
    uint8_t available = 0U;

    s_fixture->probe_calls++;
    if(hk_app_context_identity(ctx, &app_id, &generation, &owner) != HK_OK ||
       strcmp(app_id, "grant-fixture") != 0 || generation == 0U ||
       !hk_owner_is_zero(owner) ||
       hk_app_context_capability_status(
           ctx, HK_CAPABILITY_ID_TIME, 0U, &available, &fallback) != HK_OK ||
       !available || fallback != NULL ||
       hk_app_context_time(ctx, 0U, &time) != HK_ERR_INVALID_STATE)
        return HK_ERR_INTERNAL;

    if(hk_app_context_capability_status(
           ctx, HK_CAPABILITY_ID_INPUT, 0U, &available, &fallback) != HK_OK ||
       strcmp(fallback, "headless") != 0)
        return HK_ERR_INTERNAL;
    if((s_fixture->mode == MODE_OPTIONAL_ABSENT && available) ||
       (s_fixture->mode != MODE_OPTIONAL_ABSENT && !available))
        return HK_ERR_INTERNAL;
    return HK_OK;
}

static hk_result_t prepare(hk_app_context_t *ctx)
{
    hk_input_t input = {0};
    hk_lights_t lights = {0};
    hk_app_service_t service = {0};

    s_fixture->prepare_calls++;
    if(hk_app_context_time(ctx, 0U, &s_fixture->copied_time) != HK_OK ||
       !s_fixture->owner_live ||
       s_fixture->copied_time.lease.owner.generation !=
           s_fixture->live_owner.generation ||
       hk_app_context_lights(ctx, 0U, &lights) != HK_ERR_NOT_DECLARED ||
       hk_app_context_service(
           ctx, "hackylens.service.settings", &service) != HK_OK ||
       service.context_generation != ctx->generation ||
       service.owner.generation != s_fixture->live_owner.generation ||
       fake_direct_acquire(ctx->owner, HK_CAPABILITY_ID_LIGHTS) !=
           HK_ERR_NOT_DECLARED)
        return HK_ERR_INTERNAL;
    if(s_fixture->mode == MODE_OPTIONAL_ABSENT)
    {
        if(hk_app_context_input(ctx, 0U, &input) != HK_ERR_CAPABILITY_ABSENT)
            return HK_ERR_INTERNAL;
    }
    else if(hk_app_context_input(ctx, 0U, &input) != HK_OK)
    {
        return HK_ERR_INTERNAL;
    }
    s_fixture->copied_context = *ctx;
    return HK_OK;
}

static hk_result_t start(hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}

static hk_result_t event(
    hk_app_context_t *ctx,
    const hk_app_runtime_event_t *runtime_event)
{
    (void)ctx;
    (void)runtime_event;
    return HK_OK;
}

static hk_result_t tick(hk_app_context_t *ctx, uint64_t now_us)
{
    (void)ctx;
    (void)now_us;
    return HK_OK;
}

static hk_result_t render(
    hk_app_context_t *ctx,
    hk_app_runtime_surface_t *surface)
{
    (void)ctx;
    (void)surface;
    return HK_OK;
}

static hk_result_t stop(hk_app_context_t *ctx, hk_app_stop_reason_t reason)
{
    (void)ctx;
    (void)reason;
    return HK_OK;
}

static hk_result_t cleanup(hk_app_context_t *ctx)
{
    hk_time_t time = {0};

    s_fixture->cleanup_calls++;
    if(!s_fixture->owner_live ||
       hk_app_context_time(ctx, 0U, &time) != HK_OK ||
       time.lease.owner.generation != s_fixture->live_owner.generation)
        return HK_ERR_INTERNAL;
    return HK_OK;
}

static const hk_app_v2_entry_t s_entry = {
    .state_storage = s_state,
    .state_capacity_bytes = sizeof(s_state),
    .probe = probe,
    .prepare = prepare,
    .start = start,
    .event = event,
    .tick = tick,
    .render = render,
    .stop = stop,
    .cleanup = cleanup,
};

static hk_app_t descriptor(void)
{
    hk_app_t app = {0};

    app.struct_size = sizeof(app);
    app.struct_version = HK_APP_DESCRIPTOR_VERSION;
    app.id = "grant-fixture";
    app.lifecycle = HK_APP_LIFECYCLE_V2;
    app.entry.v2 = &s_entry;
    app.limits.static_ram_bytes = sizeof(s_state);
    app.limits.stack_bytes = 256U;
    app.limits.state_bytes = sizeof(s_state);
    app.limits.state_alignment = HK_APP_STATE_ALIGNMENT;
    app.limits.tick_interval_us = 20000U;
    app.limits.tick_budget_us = 1000U;
    app.limits.render_budget_us = 1000U;
    app.capabilities = s_capabilities;
    app.capability_count = 2U;
    app.services = s_services;
    app.service_count = 1U;
    return app;
}

static hk_result_t resolve_capability(
    void *user,
    const hk_app_t *app,
    const hk_app_capability_request_t *declaration,
    hk_capability_request_t *request)
{
    grants_fixture_t *fixture = user;

    (void)app;
    fixture->resolve_calls++;
    if(strcmp(declaration->id, "hackylens.cap.time") == 0)
    {
        *request = (hk_capability_request_t)HK_TIME_REQUEST_0_1_INIT;
        if(fixture->mode == MODE_REQUIRED_ABSENT)
            return HK_ERR_CAPABILITY_ABSENT;
        if(fixture->mode == MODE_VERSION_MISMATCH)
            return HK_ERR_VERSION_INCOMPATIBLE;
        if(fixture->mode == MODE_FEATURE_MISMATCH)
            return HK_ERR_FEATURE_UNAVAILABLE;
        return HK_OK;
    }
    if(strcmp(declaration->id, "hackylens.cap.input") == 0)
    {
        *request = (hk_capability_request_t)HK_INPUT_REQUEST_0_1_INIT;
        return fixture->mode == MODE_OPTIONAL_ABSENT
                   ? HK_ERR_CAPABILITY_ABSENT
                   : HK_OK;
    }
    return HK_ERR_NOT_DECLARED;
}

static hk_result_t resolve_service(
    void *user,
    const hk_app_t *app,
    const hk_app_service_request_t *declaration)
{
    (void)user;
    (void)app;
    return strcmp(declaration->id, "hackylens.service.settings") == 0
               ? HK_OK
               : HK_ERR_NOT_DECLARED;
}

static hk_result_t owner_open(
    void *user,
    const hk_app_t *app,
    hk_owner_t *owner)
{
    grants_fixture_t *fixture = user;

    (void)app;
    fixture->owner_open_calls++;
    if(fixture->mode == MODE_OWNER_EXHAUSTED)
        return HK_ERR_LIMIT;
    fixture->next_owner_generation++;
    *owner = (hk_owner_t){4U, fixture->next_owner_generation};
    fixture->live_owner = *owner;
    fixture->owner_live = 1U;
    return HK_OK;
}

static hk_result_t acquire_capability(
    void *user,
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_lease_t *lease)
{
    grants_fixture_t *fixture = user;

    fixture->acquire_calls++;
    if(!fixture->owner_live || owner.generation != fixture->live_owner.generation)
        return HK_ERR_WRONG_OWNER;
    *lease = (hk_lease_t){
        fixture->acquire_calls,
        1U,
        owner,
        request->id,
    };
    return HK_OK;
}

static hk_result_t acquire_service(
    void *user,
    hk_owner_t owner,
    const hk_app_service_request_t *declaration)
{
    grants_fixture_t *fixture = user;

    fixture->service_acquire_calls++;
    if(fixture->mode == MODE_PARTIAL_INJECTION)
        return HK_ERR_IO;
    return fixture->owner_live &&
                   owner.generation == fixture->live_owner.generation &&
                   strcmp(declaration->id, "hackylens.service.settings") == 0
               ? HK_OK
               : HK_ERR_WRONG_OWNER;
}

static hk_result_t owner_cleanup(
    void *user,
    hk_owner_t owner,
    hk_deadline_t deadline)
{
    grants_fixture_t *fixture = user;

    (void)deadline;
    fixture->owner_cleanup_calls++;
    if(!fixture->owner_live || owner.generation != fixture->live_owner.generation)
        return HK_ERR_WRONG_OWNER;
    fixture->owner_live = 0U;
    return HK_OK;
}

static hk_result_t deadline_after_us(
    void *user,
    uint64_t duration_us,
    hk_deadline_t *deadline)
{
    (void)user;
    deadline->at_us = duration_us + UINT64_C(1000);
    return HK_OK;
}

static int init_fixture(grants_fixture_t *fixture, hk_app_runtime_t *runtime)
{
    hk_app_runtime_ops_t ops = {
        .user = fixture,
        .resolve_capability = resolve_capability,
        .resolve_service = resolve_service,
        .owner_open = owner_open,
        .acquire_capability = acquire_capability,
        .acquire_service = acquire_service,
        .owner_cleanup = owner_cleanup,
        .deadline_after_us = deadline_after_us,
    };

    memset(fixture, 0, sizeof(*fixture));
    fixture->runtime = runtime;
    s_fixture = fixture;
    return hk_app_runtime_init(
        runtime, &ops, HK_APP_RUNTIME_TEARDOWN_BUDGET_US) == HK_OK ? 0 : 1;
}

static int check_preflight_failures(void)
{
    const mode_t modes[] = {
        MODE_REQUIRED_ABSENT,
        MODE_VERSION_MISMATCH,
        MODE_FEATURE_MISMATCH,
    };
    const hk_result_t expected[] = {
        HK_ERR_CAPABILITY_ABSENT,
        HK_ERR_VERSION_INCOMPATIBLE,
        HK_ERR_FEATURE_UNAVAILABLE,
    };

    for(uint32_t index = 0U; index < sizeof(modes) / sizeof(modes[0]); index++)
    {
        grants_fixture_t fixture;
        hk_app_runtime_t runtime;
        hk_app_t app = descriptor();

        CHECK(init_fixture(&fixture, &runtime) == 0);
        fixture.mode = modes[index];
        CHECK(hk_app_runtime_launch(&runtime, &app) == expected[index]);
        CHECK(fixture.resolve_calls == 1U);
        CHECK(fixture.probe_calls == 0U);
        CHECK(fixture.owner_open_calls == 0U);
        CHECK(fixture.owner_cleanup_calls == 0U);
        CHECK(hk_app_runtime_state(&runtime) == HK_APP_RUNTIME_INACTIVE);
    }
    return 0;
}

static int fake_handle_is_live(
    const grants_fixture_t *fixture,
    const hk_time_t *handle)
{
    return fixture->owner_live &&
           handle->lease.owner.slot == fixture->live_owner.slot &&
           handle->lease.owner.generation == fixture->live_owner.generation;
}

static int check_grants_and_retirement(void)
{
    grants_fixture_t fixture;
    hk_app_runtime_t runtime;
    hk_app_t app = descriptor();
    hk_time_t first_time;
    hk_app_context_t first_context;

    CHECK(init_fixture(&fixture, &runtime) == 0);
    fixture.mode = MODE_OPTIONAL_ABSENT;
    CHECK(hk_app_runtime_launch(&runtime, &app) == HK_OK);
    CHECK(fixture.resolve_calls == 2U && fixture.probe_calls == 1U);
    CHECK(fixture.owner_open_calls == 1U && fixture.acquire_calls == 1U);
    CHECK(fixture.service_acquire_calls == 1U && fixture.prepare_calls == 1U);
    CHECK(fake_handle_is_live(&fixture, &fixture.copied_time));
    first_time = fixture.copied_time;
    first_context = fixture.copied_context;
    CHECK(hk_app_runtime_stop(&runtime, HK_APP_STOP_COMPLETED) == HK_OK);
    CHECK(fixture.cleanup_calls == 1U && fixture.owner_cleanup_calls == 1U);
    CHECK(!fake_handle_is_live(&fixture, &first_time));
    CHECK(hk_app_context_time(
        &first_context, 0U, &fixture.copied_time) == HK_ERR_STALE_HANDLE);

    fixture.mode = MODE_AVAILABLE;
    CHECK(hk_app_runtime_launch(&runtime, &app) == HK_OK);
    CHECK(fixture.live_owner.generation != first_time.lease.owner.generation);
    CHECK(!fake_handle_is_live(&fixture, &first_time));
    CHECK(fake_handle_is_live(&fixture, &fixture.copied_time));
    CHECK(hk_app_runtime_stop(&runtime, HK_APP_STOP_COMPLETED) == HK_OK);
    return 0;
}

static int check_partial_injection_and_owner_exhaustion(void)
{
    grants_fixture_t fixture;
    hk_app_runtime_t runtime;
    hk_app_t app = descriptor();

    CHECK(init_fixture(&fixture, &runtime) == 0);
    fixture.mode = MODE_PARTIAL_INJECTION;
    CHECK(hk_app_runtime_launch(&runtime, &app) == HK_ERR_IO);
    CHECK(fixture.acquire_calls == 2U);
    CHECK(fixture.service_acquire_calls == 1U);
    CHECK(fixture.prepare_calls == 0U && fixture.cleanup_calls == 0U);
    CHECK(fixture.owner_cleanup_calls == 1U && !fixture.owner_live);

    CHECK(init_fixture(&fixture, &runtime) == 0);
    fixture.mode = MODE_OWNER_EXHAUSTED;
    CHECK(hk_app_runtime_launch(&runtime, &app) == HK_ERR_LIMIT);
    CHECK(fixture.probe_calls == 1U && fixture.owner_open_calls == 1U);
    CHECK(fixture.acquire_calls == 0U && fixture.prepare_calls == 0U);
    CHECK(fixture.owner_cleanup_calls == 0U);
    return 0;
}

static int check_descriptor_capacity_guards(void)
{
    grants_fixture_t fixture;
    hk_app_runtime_t runtime;
    hk_app_t app = descriptor();

    CHECK(init_fixture(&fixture, &runtime) == 0);
    app.capability_count = HK_APP_CONTEXT_MAX_CAPABILITIES + 1U;
    CHECK(hk_app_runtime_launch(&runtime, &app) == HK_ERR_INVALID_ARGUMENT);
    app = descriptor();
    app.service_count = HK_APP_CONTEXT_MAX_SERVICES + 1U;
    CHECK(hk_app_runtime_launch(&runtime, &app) == HK_ERR_INVALID_ARGUMENT);
    CHECK(fixture.resolve_calls == 0U && fixture.probe_calls == 0U);
    return 0;
}

int main(void)
{
    CHECK(check_preflight_failures() == 0);
    CHECK(check_grants_and_retirement() == 0);
    CHECK(check_partial_injection_and_owner_exhaustion() == 0);
    CHECK(check_descriptor_capacity_guards() == 0);
    puts("APP_RUNTIME_GRANTS_OK");
    return 0;
}
