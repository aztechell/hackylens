#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <hackylens/app.h>
#include <hackylens/app/camera.h>

#include "object_gate_core.h"

enum { WARMUP = 4, BENCH_FRAMES = 28 };

typedef struct
{
    object_gate_state_t gate;
    hk_app_camera_frame_t frame;
    vision_result_snapshot_t snapshot;
    const hk_time_t *clock;
    uint64_t load_started, load_us;
    uint64_t detect_started, detect_us, policy_us, present_us;
    uint64_t window_start, window_end;
    uint32_t processed, presented, digest;
    uint8_t loaded, detecting, benchmark;
} gate_app_t;

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_storage[1024];
static gate_app_t *s_active;
static uint8_t s_benchmark_next;
_Static_assert(sizeof(gate_app_t) <= sizeof(s_storage), "object gate state exceeds the runtime slot");

static uint64_t now_us(const gate_app_t *app)
{
    uint64_t now = 0;
    (void)hk_time_now_us(app->clock, &now);
    return now;
}

static hk_result_t gate_start(const hk_app_context_t *ctx)
{
    void *state;
    uint32_t capacity;
    hk_result_t result = hk_app_context_state(ctx, &state, &capacity);
    if(result != HK_OK)
        return result;
    if(capacity < sizeof(gate_app_t))
        return HK_ERR_LIMIT;
    gate_app_t *app = state;
    *app = (gate_app_t){.benchmark = s_benchmark_next};
    s_benchmark_next = 0;
    result = hk_app_context_time(ctx, &app->clock);
    if(result == HK_OK)
        result = hk_app_camera_open(ctx);
    if(result == HK_OK)
    {
        app->load_started = now_us(app);
        s_active = app;
    }
    return result;
}

static hk_result_t finish_frame(const hk_app_context_t *ctx, gate_app_t *app,
                                uint8_t measured, uint64_t detected_at)
{
    const vision_result_snapshot_t *snapshot = &app->snapshot;
    if(snapshot->frame_id != app->frame.sequence || snapshot->source != VISION_SOURCE_OBJECT_DETECT ||
       snapshot->width != app->frame.width || snapshot->height != app->frame.height)
        return HK_ERR_INTERNAL;
    uint64_t policy_start = measured ? now_us(app) : 0;
    const object_gate_view_t view = object_gate_step(&app->gate, snapshot);
    uint64_t present_start = 0;
    if(measured)
    {
        app->detect_us += detected_at - app->detect_started;
        app->policy_us += now_us(app) - policy_start;
        app->digest = object_gate_digest(app->digest, snapshot);
        present_start = now_us(app);
    }
    const hk_result_t result = hk_app_camera_present(ctx, app->frame.lease, &view.box, view.color);
    if(measured)
    {
        app->window_end = now_us(app);
        app->present_us += app->window_end - present_start;
        if(result == HK_OK)
            app->presented++;
    }
    app->processed++;
    return result == HK_ERR_BUSY ? HK_OK : result;
}

static hk_result_t gate_tick(const hk_app_context_t *ctx, gate_app_t *app)
{
    if(!app->loaded)
    {
        const hk_result_t result = hk_app_camera_model_open_step(ctx, VISION_SOURCE_OBJECT_DETECT, "object20");
        if(result == HK_OK)
        {
            app->loaded = 1U;
            app->load_us = now_us(app) - app->load_started;
        }
        return result == HK_PENDING ? HK_OK : result;
    }
    if(app->benchmark && app->processed == BENCH_FRAMES)
        return HK_OK;

    const uint8_t measured = app->benchmark && app->processed >= WARMUP;
    if(!app->detecting)
    {
        hk_result_t result = hk_app_camera_acquire(ctx, &app->frame);
        if(result != HK_OK)
            return result == HK_PENDING ? HK_OK : result;
        if(measured)
        {
            app->detect_started = now_us(app);
            if(app->processed == WARMUP)
                app->window_start = app->detect_started;
        }
        result = hk_app_camera_detect_begin(ctx, app->frame.lease);
        if(result == HK_OK)
        {
            app->detecting = 1U;
            return HK_OK;
        }
        (void)hk_app_camera_release(ctx, app->frame.lease);
        return result;
    }

    hk_result_t result = hk_app_camera_detect_poll(ctx, &app->snapshot);
    if(result == HK_PENDING)
        return HK_OK;
    if(result == HK_OK)
        result = finish_frame(ctx, app, measured, measured ? now_us(app) : 0);
    app->detecting = 0;
    const hk_result_t release = hk_app_camera_release(ctx, app->frame.lease);
    return result == HK_OK ? release : result;
}

static hk_result_t gate_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        if(event->data.input.pressed & HK_INPUT_BUTTON_BACK)
            return hk_app_context_request_close(ctx);
        if(event->data.input.pressed & HK_INPUT_BUTTON_OK)
            s_active->gate = (object_gate_state_t){0};
    }
    return event->kind == HK_APP_EVENT_TIMER ? gate_tick(ctx, s_active) : HK_OK;
}

static hk_result_t gate_stop(const hk_app_context_t *ctx)
{
    s_active = NULL;
    return hk_app_camera_close(ctx);
}

uint8_t object_gate_debug(const char *command)
{
    if(strcmp(command, "HKOGBENCH") == 0)
    {
        s_benchmark_next = 1U;
        puts("HKOGBENCH OK");
    }
    else if(strcmp(command, "HKOGINFO") == 0)
    {
        const gate_app_t *app = s_active;
        if(!app)
            puts("HKOGINFO active=0");
        else
        {
            const uint32_t complete = app->benchmark && app->processed == BENCH_FRAMES;
            printf("HKOGINFO active=1 loaded=%u processed=%" PRIu32 " occupied=%u complete=%" PRIu32 "\r\n",
                   (unsigned)app->loaded, app->processed, (unsigned)app->gate.occupied, complete);
            if(complete)
                printf("OG RESULT %u %" PRIu32 " %" PRIu32 " %" PRIu32
                       " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\r\n",
                       (unsigned)(BENCH_FRAMES - WARMUP), app->presented, app->gate.entries, app->digest,
                       app->load_us, app->detect_us, app->policy_us, app->present_us,
                       app->window_end - app->window_start);
        }
    }
    else
        return 0U;
    return 1U;
}

const hk_app_v2_entry_t object_gate_v2_entry = {
    .state_storage = s_storage,
    .state_capacity_bytes = sizeof(s_storage),
    .start = gate_start,
    .event = gate_event,
    .stop = gate_stop,
};
