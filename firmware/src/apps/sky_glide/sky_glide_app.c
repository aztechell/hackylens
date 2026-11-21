#include <hackylens/app/runtime.h>

#include "sky_glide_game.h"

#ifdef HK_SKY_GLIDE_BENCH
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { BENCH_NONE, BENCH_LOGIC, BENCH_PACED, BENCH_BURST, BENCH_INPUT };

typedef struct
{
    uint64_t start_us;
    uint64_t measured_us;
    uint64_t compute_us;
    uint64_t presses[20];
    uint32_t steps;
    uint32_t frames;
    uint32_t digest;
    uint8_t mode;
} sky_bench_t;
#endif

typedef struct
{
    sky_game_t game;
    const hk_time_t *time;
    const hk_input_t *input;
    uint64_t next_step;
    uint64_t next_frame;
    uint16_t width;
    bool ok_down;
    bool flap;
    bool dirty;
#ifdef HK_SKY_GLIDE_BENCH
    sky_bench_t bench;
#endif
} sky_app_t;

static _Alignas(HK_APP_STATE_ALIGNMENT) union
{
    sky_app_t app;
    uint8_t bytes[1024];
} storage;
_Static_assert(sizeof(sky_app_t) <= 1024U, "Sky Glide state must fit the runtime slot");
#ifdef HK_SKY_GLIDE_BENCH
static sky_app_t *active;
#endif

static sky_app_t *app_state(const hk_app_context_t *ctx)
{
    void *state = NULL;
    uint32_t capacity = 0;
    if(hk_app_context_state(ctx, &state, &capacity) != HK_OK ||
       capacity < sizeof(sky_app_t))
        return NULL;
    return state;
}

static hk_result_t sky_start(const hk_app_context_t *ctx)
{
    sky_app_t *app = app_state(ctx);
    hk_display_t *display;
    hk_display_info_t info;
    uint32_t buttons;
    uint64_t now;
    if(!app || hk_app_context_time(ctx, &app->time) != HK_OK ||
       hk_app_context_input(ctx, &app->input) != HK_OK ||
       hk_app_context_display(ctx, HK_DISPLAY_PLANE_BASE, &display) != HK_OK ||
       hk_display_get_info(display, &info) != HK_OK ||
       hk_input_get_state(app->input, &buttons) != HK_OK ||
       hk_time_now_us(app->time, &now) != HK_OK)
        return HK_ERR_INTERNAL;
    if(info.width < SKY_SIZE || info.height != SKY_SIZE)
        return HK_ERR_FEATURE_UNAVAILABLE;
    app->width = (uint16_t)info.width;
    app->ok_down = (buttons & HK_INPUT_BUTTON_OK) != 0U;
    app->next_step = now;
    app->next_frame = now;
    sky_game_reset(&app->game);
#ifdef HK_SKY_GLIDE_BENCH
    active = app;
#endif
    return HK_OK;
}

static bool update_ok(sky_app_t *app, uint32_t buttons)
{
    const bool down = (buttons & HK_INPUT_BUTTON_OK) != 0U;
    const bool pressed = down && !app->ok_down;
    app->ok_down = down;
    return pressed;
}

#ifdef HK_SKY_GLIDE_BENCH
static void replay_to(sky_app_t *app, uint32_t target)
{
    sky_bench_t *bench = &app->bench;
    while(bench->steps < target)
    {
        sky_game_step(&app->game, sky_replay_flap(bench->steps));
        bench->steps++;
    }
}

static hk_result_t benchmark_tick(const hk_app_context_t *ctx, sky_app_t *app,
                                  uint32_t buttons, uint64_t now)
{
    sky_bench_t *bench = &app->bench;
    if(bench->mode == BENCH_LOGIC)
    {
        uint64_t start;
        uint64_t finish;
        if(hk_time_now_us(app->time, &start) != HK_OK)
            return HK_ERR_INTERNAL;
        replay_to(app, bench->steps + 256U);
        if(hk_time_now_us(app->time, &finish) != HK_OK)
            return HK_ERR_INTERNAL;
        bench->compute_us += finish - start;
        bench->digest = sky_digest(bench->digest, &app->game);
        if(bench->steps == SKY_LOGIC_STEPS)
        {
            printf("SG LOGIC %u %" PRIu64 " %06x\n", (unsigned)bench->steps,
                   bench->compute_us, (unsigned)bench->digest);
            bench->mode = BENCH_NONE;
        }
        return HK_OK;
    }
    if(bench->mode == BENCH_INPUT)
    {
        if(bench->frames == 20U)
        {
            for(unsigned i = 0; i < 20U; i++)
                printf("SG PRESS %u %" PRIu64 "\n", i, bench->presses[i]);
            puts("SG INPUT DONE");
            bench->mode = BENCH_NONE;
        }
        else if(update_ok(app, buttons))
        {
            if(hk_time_now_us(app->time, &bench->presses[bench->frames]) != HK_OK)
                return HK_ERR_INTERNAL;
            app->game.y = (bench->frames++ % 2U ? 112 : 84) * 256;
            return hk_app_context_request_render(ctx, NULL);
        }
        return HK_OK;
    }
    if(bench->start_us == 0U)
        bench->start_us = now;
    if(bench->mode == BENCH_PACED)
    {
        const uint64_t elapsed = now - bench->start_us;
        const uint32_t slot = (uint32_t)(elapsed / SKY_FRAME_US);
        if(slot < 800U)
        {
            if(slot < bench->frames)
                return HK_OK;
            bench->frames = slot + 1U;
            replay_to(app, bench->frames * 2U);
            return hk_app_context_request_render(ctx, NULL);
        }
        printf("SG PACED %" PRIu64 " %" PRIu64 " %u\n",
               bench->start_us + 2000000U, bench->start_us + 32000000U, 750U);
    }
    else
    {
        if(bench->frames == 50U)
            bench->measured_us = now;
        if(bench->frames < 350U)
        {
            replay_to(app, ++bench->frames * 2U);
            return hk_app_context_request_render(ctx, NULL);
        }
        printf("SG BURST %" PRIu64 " %" PRIu64 " %u\n",
               bench->measured_us, now, 300U);
    }
    bench->mode = BENCH_NONE;
    return HK_OK;
}
#endif

static hk_result_t sky_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    sky_app_t *app = app_state(ctx);
    uint32_t buttons;
    if(!app)
        return HK_ERR_INVALID_STATE;
    if(event->kind != HK_APP_EVENT_TIMER)
        return HK_OK;
    if(hk_input_get_state(app->input, &buttons) != HK_OK)
        return HK_ERR_INTERNAL;
    if(buttons & HK_INPUT_BUTTON_BACK)
        return hk_app_context_request_close(ctx);
    uint64_t now = event->data.timer.now_us;
#ifdef HK_SKY_GLIDE_BENCH
    if(app->bench.mode != BENCH_NONE)
        return benchmark_tick(ctx, app, buttons, now);
#endif
    if(update_ok(app, buttons))
        app->flap = true;
    if(now > app->next_step + 4U * SKY_STEP_US)
        app->next_step = now - 4U * SKY_STEP_US;
    while(now >= app->next_step)
    {
        if(sky_game_step(&app->game, app->flap))
            app->dirty = true;
        app->flap = false;
        app->next_step += SKY_STEP_US;
    }
    if(app->dirty && now >= app->next_frame)
    {
        app->dirty = false;
        app->next_frame += ((now - app->next_frame) / SKY_FRAME_US + 1U) * SKY_FRAME_US;
        return hk_app_context_request_render(ctx, NULL);
    }
    return HK_OK;
}

static hk_result_t sky_render(const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    const sky_app_t *app = app_state(ctx);
    sky_rect_t rects[SKY_MAX_RECTS];
    if(!app)
        return HK_ERR_INVALID_STATE;
    size_t count = sky_game_scene(&app->game, app->width, rects);
    hk_result_t result = hk_app_surface_clear(surface, SKY_BACKGROUND);
    for(size_t i = 0; result == HK_OK && i < count; i++)
    {
        const hk_display_rect_t bounds = {
            rects[i].x, rects[i].y, rects[i].width, rects[i].height
        };
        result = hk_app_surface_fill_rect(surface, &bounds, rects[i].color);
    }
    return result;
}

static hk_result_t sky_stop(const hk_app_context_t *ctx)
{
    (void)ctx;
#ifdef HK_SKY_GLIDE_BENCH
    active = NULL;
#endif
    return HK_OK;
}

uint8_t sky_glide_debug(const char *command)
{
#ifdef HK_SKY_GLIDE_BENCH
    static const char *const names[] = {"", "LOGIC", "PACED", "BURST", "INPUT"};
    if(strncmp(command, "HKSG", 4U) != 0)
        return 0U;
    for(uint8_t mode = BENCH_LOGIC; mode <= BENCH_INPUT; mode++)
    {
        if(strcmp(command + 4, names[mode]) == 0)
        {
            if(!active || active->bench.mode != BENCH_NONE)
                puts("SG BUSY");
            else
            {
                active->bench = (sky_bench_t){.mode = mode};
                sky_game_reset(&active->game);
                if(mode == BENCH_INPUT)
                    active->game.phase = SKY_PLAYING;
                puts("SG ARMED");
            }
            return 1U;
        }
    }
#else
    (void)command;
#endif
    return 0U;
}

const hk_app_v2_entry_t sky_glide_v2_entry = {
    .state_storage = &storage,
    .state_capacity_bytes = sizeof(storage),
    .start = sky_start,
    .event = sky_event,
    .render = sky_render,
    .stop = sky_stop,
};
