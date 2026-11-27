#include "app_runtime_host_support.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static hk_app_runtime_host_t host;
static hk_app_switch_t *switcher;
static hk_result_t (*prepare_next)(void *, const hk_app_t *);
static hk_result_t (*cleanup_next)(void *, hk_deadline_t);
static hk_result_t (*deadline_next)(void *, uint64_t, hk_deadline_t *);
static hk_app_wakeup_token_t saved_wakeup;
static hk_app_context_t saved_context;
static int fail_start;
static int fail_event;
static int close_during_start;
static int timer_overrun;
static int fail_deadline;
static int stop_overrun;
static int probe_saved_context;
static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t state[16];

static const char *outcome(hk_result_t result)
{
    switch(result)
    {
    case HK_OK: return "success";
    case HK_PENDING: return "pending";
    case HK_ERR_INVALID_ARGUMENT: return "invalid_argument";
    case HK_ERR_CANCELLED: return "cancelled";
    case HK_ERR_STALE_HANDLE: return "stale";
    case HK_ERR_INVALID_STATE: return "invalid_state";
    case HK_ERR_IO: return "io";
    case HK_ERR_INTERNAL: return "internal";
    case HK_ERR_DEADLINE_EXCEEDED: return "deadline";
    default: return "unexpected_result";
    }
}

static hk_result_t prepare(void *user, const hk_app_t *app)
{
    hk_result_t result = prepare_next(user, app);
    printf("{\"event\":\"prepare\",\"app\":\"%s\",\"outcome\":\"%s\"}\n",
           app->id, outcome(result));
    return result;
}

static hk_result_t cleanup(void *user, hk_deadline_t deadline)
{
    hk_result_t result = cleanup_next(user, deadline);
    printf("{\"event\":\"retire\",\"deadline_us\":%" PRIu64
           ",\"now_us\":%" PRIu64 ",\"outcome\":\"%s\"}\n",
           deadline.at_us, hk_app_runtime_host_now_us(&host), outcome(result));
    return result;
}

static hk_result_t create_deadline(void *user, uint64_t budget, hk_deadline_t *deadline)
{
    return fail_deadline ? HK_ERR_INTERNAL : deadline_next(user, budget, deadline);
}

static hk_result_t start(const hk_app_context_t *context)
{
    const char *app;
    uint32_t identity;
    void *storage;
    uint32_t size;
    if(hk_app_context_identity(context, &app, &identity) != HK_OK ||
       hk_app_context_state(context, &storage, &size) != HK_OK)
        return HK_ERR_INTERNAL;
    unsigned int nonzero_bytes = 0;
    for(uint32_t i = 0; i < size; ++i)
        nonzero_bytes += ((uint8_t *)storage)[i] != 0;
    memset(storage, 0xa5, size);
    printf("{\"event\":\"start\",\"app\":\"%s\",\"instance\":%" PRIu32
           ",\"state_nonzero_bytes\":%u}\n", app, identity, nonzero_bytes);
    if(close_during_start)
    {
        hk_result_t result = hk_app_switch_close(switcher, HK_APP_STOP_FORCED);
        printf("{\"event\":\"request_close\",\"outcome\":\"%s\"}\n", outcome(result));
    }
    return fail_start ? HK_ERR_IO : HK_OK;
}

static hk_result_t event(const hk_app_context_t *context, const hk_app_event_t *input)
{
    const char *app;
    uint32_t identity;
    if(hk_app_context_identity(context, &app, &identity) != HK_OK)
        return HK_ERR_INTERNAL;
    const char *kind = "unexpected_event";
    if(input->kind == HK_APP_EVENT_MEDIA)
        kind = "media";
    else if(input->kind == HK_APP_EVENT_RUNTIME_CLOSE)
        kind = "runtime_close";
    else if(input->kind == HK_APP_EVENT_WAKEUP)
        kind = "wakeup";
    else if(input->kind == HK_APP_EVENT_TIMER)
        kind = "timer";
    printf("{\"event\":\"%s\",\"app\":\"%s\",\"instance\":%" PRIu32
           ",\"sequence\":%" PRIu64 ",\"timestamp_us\":%" PRIu64,
           kind, app, identity, input->sequence, input->timestamp_us);
    if(input->kind == HK_APP_EVENT_TIMER)
        printf(",\"scheduled_us\":%" PRIu64 ",\"observed_us\":%" PRIu64,
               input->data.timer.scheduled_us, input->data.timer.now_us);
    puts("}");
    if(timer_overrun && input->kind == HK_APP_EVENT_TIMER)
        return hk_app_runtime_host_advance_us(&host, 101);
    if(input->kind == HK_APP_EVENT_MEDIA)
    {
        if(probe_saved_context)
        {
            void *borrow = NULL;
            uint32_t size = 0;
            hk_result_t result = hk_app_context_state(&saved_context, &borrow, &size);
            printf("{\"event\":\"saved_context\",\"outcome\":\"%s\",\"borrowed\":%s}\n",
                   outcome(result), borrow ? "true" : "false");
        }
        else
        {
            saved_context = *context;
            if(hk_app_context_wakeup_token(context, 7, &saved_wakeup) != HK_OK)
                return HK_ERR_INTERNAL;
        }
    }
    return fail_event && input->kind == HK_APP_EVENT_MEDIA ? HK_ERR_IO : HK_OK;
}

static hk_result_t stop(const hk_app_context_t *context)
{
    const char *app;
    uint32_t identity;
    hk_deadline_t deadline;
    if(hk_app_context_identity(context, &app, &identity) != HK_OK ||
       hk_app_context_teardown_deadline(context, &deadline) != HK_OK)
        return HK_ERR_INTERNAL;
    printf("{\"event\":\"stop\",\"app\":\"%s\",\"instance\":%" PRIu32
           ",\"deadline_us\":%" PRIu64 ",\"now_us\":%" PRIu64 "}\n",
           app, identity, deadline.at_us, hk_app_runtime_host_now_us(&host));
    if(stop_overrun)
    {
        if(hk_app_runtime_host_advance_us(&host, HK_APP_RUNTIME_HOST_TEARDOWN_BUDGET_US + 1) != HK_OK)
            return HK_ERR_INTERNAL;
        return HK_ERR_IO;
    }
    return HK_OK;
}

static const hk_app_v2_entry_t entry = {
    .state_storage = state, .state_capacity_bytes = sizeof(state),
    .start = start, .event = event, .stop = stop,
};

int main(int argc, char **argv)
{
    hk_app_t app_a;
    hk_app_t app_b;
    if(hk_app_runtime_host_init(&host) != HK_OK)
        return 2;
    switcher = hk_app_runtime_host_switch(&host);
    hk_app_runtime_t *runtime = hk_app_runtime_host_runtime(&host);
    prepare_next = runtime->ops.prepare;
    cleanup_next = runtime->ops.cleanup;
    deadline_next = runtime->ops.deadline_after_us;
    runtime->ops.prepare = prepare;
    runtime->ops.cleanup = cleanup;
    runtime->ops.deadline_after_us = create_deadline;
    hk_app_runtime_host_fill_app(&app_a, "A", &entry, sizeof(state));
    hk_app_runtime_host_fill_app(&app_b, "B", &entry, sizeof(state));
    memset(state, 0xa5, sizeof(state));

    for(int i = 1; i < argc; ++i)
    {
        const char *action = argv[i];
        hk_result_t result;
        if(strcmp(action, "prepare-fails") == 0)
        {
            hk_app_runtime_host_fail_prepare(&host, HK_ERR_IO);
            continue;
        }
        if(strcmp(action, "retire-fails") == 0)
        {
            hk_app_runtime_host_fail_cleanup(&host, HK_ERR_INTERNAL);
            continue;
        }
        if(strcmp(action, "start-fails") == 0)
        {
            fail_start = 1;
            continue;
        }
        if(strcmp(action, "event-fails") == 0)
        {
            fail_event = 1;
            continue;
        }
        if(strcmp(action, "close-during-start") == 0)
        {
            close_during_start = 1;
            continue;
        }
        if(strcmp(action, "overrun-timer") == 0)
        {
            timer_overrun = 1;
            continue;
        }
        if(strcmp(action, "fail-deadline-clock") == 0)
        {
            fail_deadline = 1;
            continue;
        }
        if(strcmp(action, "overrun-stop") == 0)
        {
            stop_overrun = 1;
            continue;
        }
        if(strcmp(action, "exhaust-instance-identity") == 0)
        {
            /* Scale the counter to its boundary without billions of fixture launches. */
            runtime->context_generation = UINT32_MAX;
            continue;
        }
        if(strcmp(action, "probe-saved-context") == 0)
        {
            probe_saved_context = 1;
            continue;
        }
        if(strncmp(action, "advance-", 8) == 0)
        {
            if(hk_app_runtime_host_advance_us(&host, strtoumax(action + 8, NULL, 10)) != HK_OK)
                return 2;
            continue;
        }
        if(strcmp(action, "open-a") == 0)
            result = hk_app_switch_open(switcher, &app_a, NULL);
        else if(strcmp(action, "open-b") == 0)
            result = hk_app_switch_open(switcher, &app_b, NULL);
        else if(strcmp(action, "open-invalid") == 0)
        {
            hk_app_t invalid = app_a;
            invalid.struct_version = 0;
            result = hk_app_switch_open(switcher, &invalid, NULL);
        }
        else if(strcmp(action, "send-media") == 0)
        {
            if(hk_app_runtime_host_advance_us(&host, 11) != HK_OK)
                return 2;
            result = hk_app_switch_media(switcher, HK_APP_MEDIA_INSERTED, 1,
                                         hk_app_runtime_host_now_us(&host));
        }
        else if(strcmp(action, "send-wakeup") == 0)
            result = hk_app_switch_wakeup(switcher, saved_wakeup,
                                          hk_app_runtime_host_now_us(&host));
        else if(strcmp(action, "close") == 0)
            result = hk_app_switch_close(switcher, HK_APP_STOP_FORCED);
        else if(strcmp(action, "poll") == 0)
            result = hk_app_switch_poll(switcher, hk_app_runtime_host_now_us(&host));
        else
            return 2;
        const hk_app_t *active = hk_app_switch_active(switcher);
        printf("{\"event\":\"return\",\"action\":\"%s\",\"outcome\":\"%s\","
               "\"active_app\":\"%s\",\"first_error\":\"%s\"}\n", action, outcome(result),
               active ? active->id : "none", outcome(hk_app_runtime_first_error(runtime)));
    }
    return 0;
}
