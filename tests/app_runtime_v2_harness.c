#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

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
    FAIL_NONE = 0,
    FAIL_PROBE,
    FAIL_INJECT_NO_OWNER,
    FAIL_INJECT_WITH_OWNER,
    FAIL_PREPARE,
    FAIL_START,
    FAIL_EVENT,
    FAIL_TICK,
    FAIL_RENDER,
    FAIL_STOP,
    FAIL_CLEANUP,
    FAIL_OWNER_CLEANUP,
    FAIL_DEADLINE,
    FAIL_INVALID_DEADLINE,
    PENDING_PROBE,
} fail_point_t;

typedef struct
{
    hk_app_runtime_t *runtime;
    fail_point_t fail;
    char trace[128];
    uint32_t trace_size;
    uint32_t deadline_calls;
    uint32_t owner_cleanup_calls;
    uint32_t stop_calls;
    uint32_t cleanup_calls;
    hk_deadline_t stop_deadline;
    hk_deadline_t cleanup_deadline;
    hk_deadline_t owner_deadline;
    hk_app_stop_reason_t observed_reason;
    hk_app_runtime_token_t token;
    hk_result_t reentrant_stop;
    hk_result_t reentrant_event;
    uint8_t test_reentrant;
    volatile uint32_t callback_count;
} fixture_t;

static fixture_t *s_fixture;
static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state[64];

static void trace(char value)
{
    if(s_fixture->trace_size + 1U < sizeof(s_fixture->trace))
    {
        s_fixture->trace[s_fixture->trace_size++] = value;
        s_fixture->trace[s_fixture->trace_size] = '\0';
    }
}

static hk_result_t callback_state(const hk_app_context_t *ctx)
{
    void *state = NULL;
    uint32_t size = 0U;
    hk_result_t result = hk_app_context_state(ctx, &state, &size);

    if(result != HK_OK || state != s_state || size != sizeof(s_state))
        return HK_ERR_INTERNAL;
    return HK_OK;
}

static hk_result_t fake_probe(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline;

    trace('P');
    if(hk_app_runtime_state(s_fixture->runtime) != HK_APP_RUNTIME_INACTIVE ||
       hk_app_runtime_stage(s_fixture->runtime) != HK_APP_STAGE_PROBING ||
       !hk_owner_is_zero(ctx->owner) ||
       callback_state(ctx) != HK_OK ||
       hk_app_context_teardown_deadline(ctx, &deadline) != HK_ERR_INVALID_STATE)
        return HK_ERR_INTERNAL;
    if(s_fixture->fail == PENDING_PROBE)
        return HK_PENDING;
    return s_fixture->fail == FAIL_PROBE ? HK_ERR_IO : HK_OK;
}

static hk_result_t fake_prepare(const hk_app_context_t *ctx)
{
    trace('A');
    if(hk_app_runtime_state(s_fixture->runtime) != HK_APP_RUNTIME_INJECTING ||
       hk_app_runtime_stage(s_fixture->runtime) != HK_APP_STAGE_PREPARING ||
       ctx->owner.slot != 3U || ctx->owner.generation != 7U ||
       callback_state(ctx) != HK_OK)
        return HK_ERR_INTERNAL;
    s_state[0] = 0x5aU;
    return s_fixture->fail == FAIL_PREPARE ? HK_ERR_IO : HK_OK;
}

static hk_result_t fake_start(const hk_app_context_t *ctx)
{
    hk_app_runtime_event_t event = {0U, 0U};

    trace('S');
    if(hk_app_runtime_state(s_fixture->runtime) != HK_APP_RUNTIME_PREPARED ||
       hk_app_runtime_stage(s_fixture->runtime) != HK_APP_STAGE_STARTING ||
       ctx->owner.slot != 3U || ctx->owner.generation != 7U ||
       callback_state(ctx) != HK_OK ||
       hk_app_context_deferred_token(ctx, &s_fixture->token) != HK_ERR_INVALID_STATE)
        return HK_ERR_INTERNAL;
    if(s_fixture->test_reentrant)
    {
        s_fixture->reentrant_stop = hk_app_runtime_stop(
            s_fixture->runtime, HK_APP_STOP_SWITCH);
        s_fixture->reentrant_event = hk_app_runtime_event(
            s_fixture->runtime, &event);
    }
    return s_fixture->fail == FAIL_START ? HK_ERR_IO : HK_OK;
}

static hk_result_t fake_event(
    const hk_app_context_t *ctx,
    const hk_app_runtime_event_t *event)
{
    (void)event;
    trace('E');
    s_fixture->callback_count++;
    if(hk_app_runtime_state(s_fixture->runtime) != HK_APP_RUNTIME_RUNNING ||
       hk_app_runtime_stage(s_fixture->runtime) != HK_APP_STAGE_RUNNING ||
       callback_state(ctx) != HK_OK)
        return HK_ERR_INTERNAL;
    if(s_fixture->token.slot == 0U &&
       hk_app_context_deferred_token(ctx, &s_fixture->token) != HK_OK)
        return HK_ERR_INTERNAL;
    if(s_fixture->test_reentrant)
    {
        s_fixture->reentrant_stop = hk_app_runtime_stop(
            s_fixture->runtime, HK_APP_STOP_SWITCH);
        s_fixture->reentrant_event = hk_app_runtime_event(
            s_fixture->runtime, event);
    }
    return s_fixture->fail == FAIL_EVENT ? HK_ERR_IO : HK_OK;
}

static hk_result_t fake_tick(const hk_app_context_t *ctx, uint64_t now_us)
{
    (void)now_us;
    trace('T');
    if(hk_app_runtime_state(s_fixture->runtime) != HK_APP_RUNTIME_RUNNING ||
       hk_app_runtime_stage(s_fixture->runtime) != HK_APP_STAGE_RUNNING ||
       callback_state(ctx) != HK_OK)
        return HK_ERR_INTERNAL;
    return s_fixture->fail == FAIL_TICK ? HK_ERR_IO : HK_OK;
}

static hk_result_t fake_render(
    const hk_app_context_t *ctx,
    hk_app_runtime_surface_t *surface)
{
    (void)surface;
    trace('R');
    if(hk_app_runtime_state(s_fixture->runtime) != HK_APP_RUNTIME_RUNNING ||
       hk_app_runtime_stage(s_fixture->runtime) != HK_APP_STAGE_RUNNING ||
       callback_state(ctx) != HK_OK)
        return HK_ERR_INTERNAL;
    return s_fixture->fail == FAIL_RENDER ? HK_ERR_IO : HK_OK;
}

static hk_result_t fake_stop(
    const hk_app_context_t *ctx,
    hk_app_stop_reason_t reason)
{
    trace('X');
    s_fixture->stop_calls++;
    s_fixture->observed_reason = reason;
    if(hk_app_runtime_state(s_fixture->runtime) != HK_APP_RUNTIME_STOPPING ||
       hk_app_runtime_stage(s_fixture->runtime) != HK_APP_STAGE_STOPPING ||
       ctx->owner.slot != 3U || ctx->owner.generation != 7U ||
       callback_state(ctx) != HK_OK ||
       hk_app_context_teardown_deadline(
           ctx, &s_fixture->stop_deadline) != HK_OK)
        return HK_ERR_INTERNAL;
    CHECK(hk_app_runtime_validate_token(
        s_fixture->runtime, s_fixture->token) == HK_ERR_STALE_HANDLE);
    return s_fixture->fail == FAIL_STOP ? HK_ERR_IO : HK_OK;
}

static hk_result_t fake_cleanup(const hk_app_context_t *ctx)
{
    trace('C');
    s_fixture->cleanup_calls++;
    if(hk_app_runtime_state(s_fixture->runtime) != HK_APP_RUNTIME_CLEANING ||
       hk_app_runtime_stage(s_fixture->runtime) != HK_APP_STAGE_APP_CLEANUP ||
       ctx->owner.slot != 3U || ctx->owner.generation != 7U ||
       callback_state(ctx) != HK_OK ||
       hk_app_context_teardown_deadline(
           ctx, &s_fixture->cleanup_deadline) != HK_OK)
        return HK_ERR_INTERNAL;
    return s_fixture->fail == FAIL_CLEANUP ? HK_ERR_IO : HK_OK;
}

static const hk_app_v2_entry_t s_entry = {
    .state_storage = s_state,
    .state_capacity_bytes = sizeof(s_state),
    .probe = fake_probe,
    .prepare = fake_prepare,
    .start = fake_start,
    .event = fake_event,
    .tick = fake_tick,
    .render = fake_render,
    .stop = fake_stop,
    .cleanup = fake_cleanup,
};

static hk_app_t descriptor(void)
{
    hk_app_t app = {0};

    app.struct_size = sizeof(hk_app_t);
    app.struct_version = HK_APP_DESCRIPTOR_VERSION;
    app.id = "fixture";
    app.lifecycle = HK_APP_LIFECYCLE_V2;
    app.entry.v2 = &s_entry;
    app.limits.static_ram_bytes = sizeof(s_state);
    app.limits.stack_bytes = 256U;
    app.limits.state_bytes = sizeof(s_state);
    app.limits.state_alignment = HK_APP_STATE_ALIGNMENT;
    app.limits.tick_interval_us = 20000U;
    app.limits.tick_budget_us = 1000U;
    app.limits.render_budget_us = 1000U;
    return app;
}

static hk_result_t fake_resolve_capability(
    void *user,
    const hk_app_t *app,
    const hk_app_capability_request_t *declaration,
    hk_capability_request_t *request)
{
    (void)user;
    (void)app;
    (void)declaration;
    (void)request;
    return HK_ERR_INTERNAL;
}

static hk_result_t fake_resolve_service(
    void *user,
    const hk_app_t *app,
    const hk_app_service_request_t *declaration)
{
    (void)user;
    (void)app;
    (void)declaration;
    return HK_ERR_INTERNAL;
}

static hk_result_t fake_acquire_capability(
    void *user,
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_lease_t *lease)
{
    (void)user;
    (void)owner;
    (void)request;
    (void)lease;
    return HK_ERR_INTERNAL;
}

static hk_result_t fake_acquire_service(
    void *user,
    hk_owner_t owner,
    const hk_app_service_request_t *declaration)
{
    (void)user;
    (void)owner;
    (void)declaration;
    return HK_ERR_INTERNAL;
}

static hk_result_t fake_owner_open(
    void *user,
    const hk_app_t *app,
    hk_owner_t *owner)
{
    fixture_t *fixture = user;

    (void)app;
    trace('I');
    if(hk_app_runtime_state(fixture->runtime) != HK_APP_RUNTIME_INJECTING ||
       hk_app_runtime_stage(fixture->runtime) != HK_APP_STAGE_INJECTING)
        return HK_ERR_INTERNAL;
    if(fixture->fail != FAIL_INJECT_NO_OWNER)
        *owner = (hk_owner_t){3U, 7U};
    if(fixture->fail == FAIL_INJECT_NO_OWNER ||
       fixture->fail == FAIL_INJECT_WITH_OWNER)
        return HK_ERR_NOT_DECLARED;
    return HK_OK;
}

static hk_result_t fake_owner_cleanup(
    void *user,
    hk_owner_t owner,
    hk_deadline_t deadline)
{
    fixture_t *fixture = user;

    trace('O');
    fixture->owner_cleanup_calls++;
    fixture->owner_deadline = deadline;
    if(hk_app_runtime_state(fixture->runtime) != HK_APP_RUNTIME_CLEANING ||
       hk_app_runtime_stage(fixture->runtime) != HK_APP_STAGE_OWNER_CLEANUP ||
       owner.slot != 3U || owner.generation != 7U ||
       (fixture->fail != FAIL_INJECT_WITH_OWNER && s_state[0] != 0x5aU))
        return HK_ERR_WRONG_OWNER;
    return fixture->fail == FAIL_OWNER_CLEANUP ? HK_ERR_IO : HK_OK;
}

static hk_result_t fake_deadline(
    void *user,
    uint64_t duration_us,
    hk_deadline_t *deadline)
{
    fixture_t *fixture = user;

    trace('D');
    fixture->deadline_calls++;
    if(duration_us != HK_APP_RUNTIME_TEARDOWN_BUDGET_US)
        return HK_ERR_LIMIT;
    if(fixture->fail == FAIL_DEADLINE)
        return HK_ERR_IO;
    if(fixture->fail == FAIL_INVALID_DEADLINE)
    {
        deadline->at_us = UINT64_MAX;
        return HK_OK;
    }
    deadline->at_us = UINT64_C(987654321);
    return HK_OK;
}

static int reset_fixture(fixture_t *fixture, hk_app_runtime_t *runtime)
{
    static const hk_app_runtime_ops_t ops_template = {
        .resolve_capability = fake_resolve_capability,
        .resolve_service = fake_resolve_service,
        .owner_open = fake_owner_open,
        .acquire_capability = fake_acquire_capability,
        .acquire_service = fake_acquire_service,
        .owner_cleanup = fake_owner_cleanup,
        .deadline_after_us = fake_deadline,
    };
    hk_app_runtime_ops_t ops = ops_template;

    memset(fixture, 0, sizeof(*fixture));
    memset(runtime, 0, sizeof(*runtime));
    fixture->runtime = runtime;
    s_fixture = fixture;
    ops.user = fixture;
    CHECK(hk_app_runtime_init(
        runtime, &ops, HK_APP_RUNTIME_TEARDOWN_BUDGET_US) == HK_OK);
    return 0;
}

static int check_normal_lifecycle(void)
{
    fixture_t fixture;
    hk_app_runtime_t runtime;
    hk_app_runtime_event_t event = {1U, 2U};
    hk_app_runtime_surface_t surface = {0};
    hk_app_t app = descriptor();
    hk_app_context_t *retained;

    CHECK(reset_fixture(&fixture, &runtime) == 0);
    CHECK(hk_app_runtime_launch(&runtime, &app) == HK_OK);
    CHECK(strcmp(fixture.trace, "PIAS") == 0);
    CHECK(hk_app_runtime_state(&runtime) == HK_APP_RUNTIME_RUNNING);
    CHECK(hk_app_runtime_stage(&runtime) == HK_APP_STAGE_RUNNING);
    retained = &runtime.context;
    CHECK(hk_app_context_state(retained, &surface.view, &surface.flags) ==
          HK_ERR_WRONG_CONTEXT);
    CHECK(hk_app_runtime_event(&runtime, &event) == HK_OK);
    CHECK(hk_app_runtime_validate_token(&runtime, fixture.token) == HK_OK);
    CHECK(hk_app_runtime_tick(&runtime, 123U) == HK_OK);
    CHECK(hk_app_runtime_render(&runtime, &surface) == HK_OK);
    CHECK(hk_app_runtime_stop(&runtime, HK_APP_STOP_BACK) == HK_OK);
    CHECK(strcmp(fixture.trace, "PIASETRDXCO") == 0);
    CHECK(fixture.deadline_calls == 1U);
    CHECK(fixture.owner_cleanup_calls == 1U);
    CHECK(fixture.stop_calls == 1U && fixture.cleanup_calls == 1U);
    CHECK(fixture.observed_reason == HK_APP_STOP_BACK);
    CHECK(fixture.stop_deadline.at_us == UINT64_C(987654321));
    CHECK(fixture.cleanup_deadline.at_us == fixture.stop_deadline.at_us);
    CHECK(fixture.owner_deadline.at_us == fixture.stop_deadline.at_us);
    CHECK(hk_app_runtime_validate_token(&runtime, fixture.token) ==
          HK_ERR_STALE_HANDLE);
    CHECK(hk_app_context_teardown_deadline(
        retained, &fixture.stop_deadline) == HK_ERR_STALE_HANDLE);
    CHECK(hk_app_runtime_state(&runtime) == HK_APP_RUNTIME_INACTIVE);
    CHECK(hk_app_runtime_stage(&runtime) == HK_APP_STAGE_REUSABLE);
    CHECK(hk_app_runtime_stop(&runtime, HK_APP_STOP_SWITCH) == HK_OK);
    CHECK(fixture.stop_calls == 1U && fixture.cleanup_calls == 1U);
    for(uint32_t index = 0U; index < sizeof(s_state); index++)
        CHECK(s_state[index] == 0U);

    {
        hk_app_runtime_token_t stale = fixture.token;

        fixture.token = (hk_app_runtime_token_t){0U, 0U, 0U};
        fixture.trace_size = 0U;
        fixture.trace[0] = '\0';
        CHECK(hk_app_runtime_launch(&runtime, &app) == HK_OK);
        CHECK(hk_app_runtime_validate_token(&runtime, stale) ==
              HK_ERR_STALE_HANDLE);
        CHECK(hk_app_runtime_event(&runtime, &event) == HK_OK);
        CHECK(hk_app_runtime_validate_token(&runtime, fixture.token) == HK_OK);
        CHECK(hk_app_runtime_stop(&runtime, HK_APP_STOP_COMPLETED) == HK_OK);
    }
    return 0;
}

static int check_launch_faults(void)
{
    static const struct
    {
        fail_point_t fail;
        hk_result_t expected;
        const char *trace;
        uint32_t stop_calls;
        uint32_t cleanup_calls;
        uint32_t owner_calls;
        uint32_t deadline_calls;
    } cases[] = {
        {FAIL_PROBE, HK_ERR_IO, "P", 0U, 0U, 0U, 0U},
        {PENDING_PROBE, HK_ERR_INVALID_STATE, "P", 0U, 0U, 0U, 0U},
        {FAIL_INJECT_NO_OWNER, HK_ERR_NOT_DECLARED, "PI", 0U, 0U, 0U, 0U},
        {FAIL_INJECT_WITH_OWNER, HK_ERR_NOT_DECLARED, "PIDO", 0U, 0U, 1U, 1U},
        {FAIL_PREPARE, HK_ERR_IO, "PIADCO", 0U, 1U, 1U, 1U},
        {FAIL_START, HK_ERR_IO, "PIASDXCO", 1U, 1U, 1U, 1U},
    };

    for(uint32_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++)
    {
        fixture_t fixture;
        hk_app_runtime_t runtime;
        hk_app_t app = descriptor();

        CHECK(reset_fixture(&fixture, &runtime) == 0);
        fixture.fail = cases[index].fail;
        CHECK(hk_app_runtime_launch(&runtime, &app) == cases[index].expected);
        CHECK(strcmp(fixture.trace, cases[index].trace) == 0);
        CHECK(fixture.stop_calls == cases[index].stop_calls);
        CHECK(fixture.cleanup_calls == cases[index].cleanup_calls);
        CHECK(fixture.owner_cleanup_calls == cases[index].owner_calls);
        CHECK(fixture.deadline_calls == cases[index].deadline_calls);
        CHECK(hk_app_runtime_state(&runtime) == HK_APP_RUNTIME_INACTIVE);
    }
    return 0;
}

static int check_running_faults(void)
{
    static const fail_point_t failures[] = {
        FAIL_EVENT, FAIL_TICK, FAIL_RENDER,
    };

    for(uint32_t index = 0U; index < sizeof(failures) / sizeof(failures[0]); index++)
    {
        fixture_t fixture;
        hk_app_runtime_t runtime;
        hk_app_runtime_event_t event = {0U, 0U};
        hk_app_runtime_surface_t surface = {0};
        hk_app_t app = descriptor();
        hk_result_t result;

        CHECK(reset_fixture(&fixture, &runtime) == 0);
        CHECK(hk_app_runtime_launch(&runtime, &app) == HK_OK);
        fixture.fail = failures[index];
        if(failures[index] == FAIL_EVENT)
            result = hk_app_runtime_event(&runtime, &event);
        else if(failures[index] == FAIL_TICK)
            result = hk_app_runtime_tick(&runtime, 1U);
        else
            result = hk_app_runtime_render(&runtime, &surface);
        CHECK(result == HK_ERR_IO);
        CHECK(fixture.observed_reason == HK_APP_STOP_CALLBACK_FAILED);
        CHECK(fixture.stop_calls == 1U && fixture.cleanup_calls == 1U);
        CHECK(fixture.owner_cleanup_calls == 1U && fixture.deadline_calls == 1U);
        CHECK(hk_app_runtime_state(&runtime) == HK_APP_RUNTIME_INACTIVE);
    }
    return 0;
}

static int check_teardown_faults_and_reentrancy(void)
{
    static const fail_point_t failures[] = {
        FAIL_DEADLINE, FAIL_INVALID_DEADLINE, FAIL_STOP, FAIL_CLEANUP,
        FAIL_OWNER_CLEANUP,
    };

    for(uint32_t index = 0U; index < sizeof(failures) / sizeof(failures[0]); index++)
    {
        fixture_t fixture;
        hk_app_runtime_t runtime;
        hk_app_t app = descriptor();

        CHECK(reset_fixture(&fixture, &runtime) == 0);
        CHECK(hk_app_runtime_launch(&runtime, &app) == HK_OK);
        fixture.fail = failures[index];
        CHECK(hk_app_runtime_stop(&runtime, HK_APP_STOP_SWITCH) ==
              (failures[index] == FAIL_INVALID_DEADLINE ? HK_ERR_INTERNAL :
                                                        HK_ERR_IO));
        CHECK(fixture.deadline_calls == 1U);
        CHECK(fixture.stop_calls == 1U);
        CHECK(fixture.cleanup_calls == 1U);
        CHECK(fixture.owner_cleanup_calls == 1U);
        CHECK(hk_app_runtime_state(&runtime) == HK_APP_RUNTIME_INACTIVE);
        if(failures[index] == FAIL_DEADLINE ||
           failures[index] == FAIL_INVALID_DEADLINE)
        {
            CHECK(fixture.stop_deadline.at_us == 0U);
            CHECK(fixture.cleanup_deadline.at_us == 0U);
            CHECK(fixture.owner_deadline.at_us == 0U);
        }
    }

    {
        fixture_t fixture;
        hk_app_runtime_t runtime;
        hk_app_runtime_event_t event = {0U, 0U};
        hk_app_t app = descriptor();

        CHECK(reset_fixture(&fixture, &runtime) == 0);
        fixture.test_reentrant = 1U;
        CHECK(hk_app_runtime_launch(&runtime, &app) == HK_OK);
        CHECK(fixture.reentrant_stop == HK_ERR_INVALID_STATE);
        CHECK(fixture.reentrant_event == HK_ERR_INVALID_STATE);
        CHECK(hk_app_runtime_event(&runtime, &event) == HK_OK);
        CHECK(fixture.reentrant_stop == HK_ERR_INVALID_STATE);
        CHECK(fixture.reentrant_event == HK_ERR_INVALID_STATE);
        CHECK(hk_app_runtime_stop(&runtime, HK_APP_STOP_COMPLETED) == HK_OK);
    }
    return 0;
}

static int check_stop_reasons_and_descriptor_guards(void)
{
    for(unsigned reason = 0U; reason <= 8U; reason++)
    {
        fixture_t fixture;
        hk_app_runtime_t runtime;
        hk_app_t app = descriptor();

        CHECK(reset_fixture(&fixture, &runtime) == 0);
        CHECK(hk_app_runtime_launch(&runtime, &app) == HK_OK);
        CHECK(hk_app_runtime_stop(
            &runtime, (hk_app_stop_reason_t)reason) == HK_OK);
        CHECK(fixture.observed_reason ==
              (reason <= HK_APP_STOP_SHUTDOWN ? (hk_app_stop_reason_t)reason :
                                                HK_APP_STOP_FORCED));
    }

    {
        fixture_t fixture;
        hk_app_runtime_t runtime;
        hk_app_t app = descriptor();
        hk_app_v2_entry_t bad_entry = s_entry;

        CHECK(reset_fixture(&fixture, &runtime) == 0);
        app.limits.state_bytes = sizeof(s_state) + 1U;
        CHECK(hk_app_runtime_launch(&runtime, &app) == HK_ERR_INVALID_ARGUMENT);
        app = descriptor();
        app.limits.state_alignment = 32U;
        CHECK(hk_app_runtime_launch(&runtime, &app) == HK_ERR_INVALID_ARGUMENT);
        app = descriptor();
        app.limits.state_alignment = 8U;
        CHECK(hk_app_runtime_launch(&runtime, &app) == HK_ERR_INVALID_ARGUMENT);
        app = descriptor();
        app.limits.tick_budget_us = app.limits.tick_interval_us + 1U;
        CHECK(hk_app_runtime_launch(&runtime, &app) == HK_ERR_INVALID_ARGUMENT);
        app = descriptor();
        bad_entry.state_storage = &s_state[1];
        app.entry.v2 = &bad_entry;
        CHECK(hk_app_runtime_launch(&runtime, &app) == HK_ERR_INVALID_ARGUMENT);
        app = descriptor();
        app.lifecycle = HK_APP_LIFECYCLE_LEGACY;
        CHECK(hk_app_runtime_launch(&runtime, &app) == HK_ERR_INVALID_ARGUMENT);
    }
    return 0;
}

static int check_generation_retirement(void)
{
    fixture_t fixture;
    hk_app_runtime_t runtime;
    hk_app_t app = descriptor();

    CHECK(reset_fixture(&fixture, &runtime) == 0);
    runtime.context_generation = UINT32_MAX;
    CHECK(hk_app_runtime_launch(&runtime, &app) == HK_OK);
    CHECK(hk_app_runtime_stop(&runtime, HK_APP_STOP_COMPLETED) == HK_OK);
    CHECK(hk_app_runtime_state(&runtime) == HK_APP_RUNTIME_FAULTED);
    CHECK(hk_app_runtime_launch(&runtime, &app) == HK_ERR_INVALID_STATE);
    return 0;
}

static uint64_t monotonic_ns(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    uint64_t seconds;
    uint64_t remainder;

    if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
       !QueryPerformanceCounter(&counter) || counter.QuadPart < 0)
        return 0U;
    seconds = (uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart;
    remainder = (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart;
    return seconds * UINT64_C(1000000000) +
           remainder * UINT64_C(1000000000) /
               (uint64_t)frequency.QuadPart;
#else
    struct timespec value;

    if(clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return 0U;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
#endif
}

static int compare_u64(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static hk_result_t benchmark_callback(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}

static hk_result_t benchmark_event(
    const hk_app_context_t *ctx,
    const hk_app_runtime_event_t *event)
{
    (void)ctx;
    (void)event;
    return HK_OK;
}

static hk_result_t benchmark_tick(
    const hk_app_context_t *ctx,
    uint64_t now_us)
{
    (void)ctx;
    (void)now_us;
    return HK_OK;
}

static hk_result_t benchmark_render(
    const hk_app_context_t *ctx,
    hk_app_runtime_surface_t *surface)
{
    (void)ctx;
    (void)surface;
    return HK_OK;
}

static hk_result_t benchmark_stop(
    const hk_app_context_t *ctx,
    hk_app_stop_reason_t reason)
{
    (void)ctx;
    (void)reason;
    return HK_OK;
}

static hk_result_t benchmark_owner_open(
    void *user,
    const hk_app_t *descriptor,
    hk_owner_t *owner)
{
    (void)user;
    (void)descriptor;
    *owner = (hk_owner_t){1U, 1U};
    return HK_OK;
}

static hk_result_t benchmark_owner_cleanup(
    void *user,
    hk_owner_t owner,
    hk_deadline_t deadline)
{
    (void)user;
    (void)owner;
    (void)deadline;
    return HK_OK;
}

static hk_result_t benchmark_deadline(
    void *user,
    uint64_t duration_us,
    hk_deadline_t *deadline)
{
    (void)user;
    deadline->at_us = duration_us;
    return HK_OK;
}

static int check_lifecycle_latency(
    uint64_t *event_p99_ns,
    uint64_t *launch_p99_ns,
    uint64_t *stop_p99_ns)
{
    enum { SAMPLE_COUNT = 101, ITERATIONS = 1000 };
    static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t benchmark_state[16];
    static const hk_app_v2_entry_t benchmark_entry = {
        .state_storage = benchmark_state,
        .state_capacity_bytes = sizeof(benchmark_state),
        .probe = benchmark_callback,
        .prepare = benchmark_callback,
        .start = benchmark_callback,
        .event = benchmark_event,
        .tick = benchmark_tick,
        .render = benchmark_render,
        .stop = benchmark_stop,
        .cleanup = benchmark_callback,
    };
    const hk_app_runtime_ops_t ops = {
        .resolve_capability = fake_resolve_capability,
        .resolve_service = fake_resolve_service,
        .owner_open = benchmark_owner_open,
        .acquire_capability = fake_acquire_capability,
        .acquire_service = fake_acquire_service,
        .owner_cleanup = benchmark_owner_cleanup,
        .deadline_after_us = benchmark_deadline,
    };
    hk_app_runtime_t runtime;
    hk_app_runtime_event_t event = {0U, 0U};
    hk_app_t app = descriptor();
    uint64_t event_samples[SAMPLE_COUNT];
    uint64_t launch_samples[SAMPLE_COUNT];
    uint64_t stop_samples[SAMPLE_COUNT];

    app.entry.v2 = &benchmark_entry;
    app.limits.static_ram_bytes = sizeof(benchmark_state);
    app.limits.state_bytes = sizeof(benchmark_state);
    CHECK(hk_app_runtime_init(
        &runtime, &ops, HK_APP_RUNTIME_TEARDOWN_BUDGET_US) == HK_OK);
    CHECK(hk_app_runtime_launch(&runtime, &app) == HK_OK);
    for(uint32_t sample = 0U; sample < SAMPLE_COUNT; sample++)
    {
        uint64_t started = monotonic_ns();
        CHECK(started != 0U);
        for(uint32_t iteration = 0U; iteration < ITERATIONS; iteration++)
            CHECK(hk_app_runtime_event(&runtime, &event) == HK_OK);
        event_samples[sample] = (monotonic_ns() - started) / ITERATIONS;
    }
    CHECK(hk_app_runtime_stop(&runtime, HK_APP_STOP_COMPLETED) == HK_OK);

    for(uint32_t sample = 0U; sample < SAMPLE_COUNT; sample++)
    {
        uint64_t launch_total = 0U;
        uint64_t stop_total = 0U;

        for(uint32_t iteration = 0U; iteration < ITERATIONS; iteration++)
        {
            uint64_t started = monotonic_ns();
            CHECK(started != 0U);
            CHECK(hk_app_runtime_launch(&runtime, &app) == HK_OK);
            launch_total += monotonic_ns() - started;

            started = monotonic_ns();
            CHECK(started != 0U);
            CHECK(hk_app_runtime_stop(
                &runtime, HK_APP_STOP_COMPLETED) == HK_OK);
            stop_total += monotonic_ns() - started;
        }
        launch_samples[sample] = launch_total / ITERATIONS;
        stop_samples[sample] = stop_total / ITERATIONS;
    }

    qsort(event_samples, SAMPLE_COUNT, sizeof(event_samples[0]), compare_u64);
    qsort(launch_samples, SAMPLE_COUNT, sizeof(launch_samples[0]), compare_u64);
    qsort(stop_samples, SAMPLE_COUNT, sizeof(stop_samples[0]), compare_u64);
    *event_p99_ns = event_samples[99];
    *launch_p99_ns = launch_samples[99];
    *stop_p99_ns = stop_samples[99];
    CHECK(*event_p99_ns <= UINT64_C(100000));
    CHECK(*launch_p99_ns <= UINT64_C(100000));
    CHECK(*stop_p99_ns <= UINT64_C(100000));
    return 0;
}

int main(void)
{
    uint64_t event_p99_ns = 0U;
    uint64_t launch_p99_ns = 0U;
    uint64_t stop_p99_ns = 0U;

    CHECK(check_normal_lifecycle() == 0);
    CHECK(check_launch_faults() == 0);
    CHECK(check_running_faults() == 0);
    CHECK(check_teardown_faults_and_reentrancy() == 0);
    CHECK(check_stop_reasons_and_descriptor_guards() == 0);
    CHECK(check_generation_retirement() == 0);
    CHECK(check_lifecycle_latency(
        &event_p99_ns, &launch_p99_ns, &stop_p99_ns) == 0);
    printf(
        "APP_RUNTIME_V2_OK host_event_p99_ns=%llu host_launch_p99_ns=%llu "
        "host_stop_p99_ns=%llu limit_us=100 "
        "samples=101 iterations=1000\n",
        (unsigned long long)event_p99_ns,
        (unsigned long long)launch_p99_ns,
        (unsigned long long)stop_p99_ns);
    return 0;
}
