#include "hk_main.h"

#include <stdio.h>
#include <string.h>

#include <hackylens/capability/input.h>

#include "app_runtime_integration.h"
#include "../core/hk_app_registry.h"
#include "../core/hk_dispatch.h"
#include "../core/hk_menu_runtime.h"
#include "../core/hk_screen.h"
#include "../core/hk_capability_client.h"
#include "hal_time.h"

static hk_main_hooks_t s_hooks;

static hk_input_t s_runtime_input;
static hk_owner_t s_runtime_input_owner;

static hk_result_t runtime_input_prepare(void)
{
    static const hk_capability_request_t request = HK_INPUT_REQUEST_0_1_INIT;

    s_runtime_input_owner = capability_client_consumer_owner(
        "consumer:firmware-runtime");
    if(hk_owner_is_zero(s_runtime_input_owner))
        return HK_ERR_STALE_HANDLE;
    return hk_input_acquire(
        s_runtime_input_owner, &request, &s_runtime_input);
}

static hk_result_t runtime_input_sample(uint32_t *state)
{
    return hk_input_get_state(
        s_runtime_input_owner, &s_runtime_input, state);
}

static hk_result_t runtime_input_dispatch(hk_input_snapshot_t *snapshot)
{
    hk_input_event_t event;
    hk_result_t result;

    if(!snapshot)
        return HK_ERR_INVALID_ARGUMENT;
    *snapshot = (hk_input_snapshot_t){0U, 0U, 0U};
    result = runtime_input_sample(&snapshot->state);
    if(result != HK_OK)
        return result;
    while((result = hk_input_next_event(
               s_runtime_input_owner, &s_runtime_input, &event)) == HK_OK)
    {
        uint8_t consumed = 0U;

        snapshot->state = event.state;
        snapshot->pressed = event.pressed;
        snapshot->changed = event.changed;
        activity_note();
        result = app_runtime_integration_input(&event, &consumed);
        if(result != HK_OK && result != HK_PENDING)
        {
            if(hk_screen_get() == SCREEN_APP_SLOT_0)
                shell_show_menu_reason(HK_APP_STOP_CALLBACK_FAILED);
            return HK_OK;
        }
        if(consumed && !app_runtime_integration_active() &&
           hk_screen_get() == SCREEN_APP_SLOT_0)
            shell_show_menu_reason(HK_APP_STOP_COMPLETED);
        if(!consumed)
            shell_handle_buttons(snapshot);
    }
    if(result == HK_ERR_OVERFLOW)
    {
        uint8_t consumed = 0U;

        snapshot->state = event.state;
        snapshot->pressed = 0U;
        snapshot->changed = 0U;
        result = app_runtime_integration_input(&event, &consumed);
        if(result != HK_OK && result != HK_PENDING)
        {
            if(hk_screen_get() == SCREEN_APP_SLOT_0)
                shell_show_menu_reason(HK_APP_STOP_CALLBACK_FAILED);
        }
        return HK_OK;
    }
    return result == HK_PENDING ? HK_OK : result;
}

void hk_main_set_hooks(const hk_main_hooks_t *hooks)
{
    if(hooks)
        s_hooks = *hooks;
    else
        memset(&s_hooks, 0, sizeof(s_hooks));
}

int hk_main(void)
{
    uint64_t next_dispatch_us = 0U;

    if(s_hooks.startup)
        s_hooks.startup();
    if(runtime_input_prepare() != HK_OK)
    {
        printf("[INPUT] capability unavailable\r\n");
        return -1;
    }
    while(1)
    {
        const hk_app_t *app;
        const hk_legacy_app_entry_t *entry;
        hk_input_snapshot_t input;
        uint64_t now_us;
        uint64_t sleep_us;
        uint32_t tick_interval_us;
        hk_result_t poll_result;

        input = (hk_input_snapshot_t){0U, 0U, 0U};
        if(runtime_input_sample(&input.state) != HK_OK)
        {
            printf("[INPUT] provider failure\r\n");
            return -1;
        }
        if(app_runtime_integration_now_us(&now_us) != HK_OK)
        {
            static uint8_t s_time_fallback;
            if(!s_time_fallback)
            {
                printf("[TIME] capability failure; using HAL clock\r\n");
                s_time_fallback = 1U;
            }
            now_us = hal_time_us();
        }
        if(next_dispatch_us != 0U && now_us < next_dispatch_us)
        {
            sleep_us = next_dispatch_us - now_us;
            if(sleep_us > HK_INPUT_SAMPLE_INTERVAL_US)
                sleep_us = HK_INPUT_SAMPLE_INTERVAL_US;
            hal_sleep_ms((uint32_t)((sleep_us + 999U) / 1000U));
            continue;
        }
        if(runtime_input_dispatch(&input) != HK_OK)
        {
            printf("[INPUT] event dispatch failure\r\n");
            return -1;
        }
        if(s_hooks.debug_tick)
            s_hooks.debug_tick();
        if(hk_screen_get() == SCREEN_MENU)
            menu_tick(&input);
        app = hk_app_for_screen(hk_screen_get());
        entry = hk_app_legacy_entry(app);
        if(entry && entry->tick)
            entry->tick(&input);
        if(s_hooks.system_tick)
            s_hooks.system_tick(&input);
        poll_result = app_runtime_integration_poll(now_us);
        if(poll_result != HK_OK &&
           hk_screen_get() == SCREEN_APP_SLOT_0)
        {
            printf("[APP] poll failed result=%d\r\n", (int)poll_result);
            shell_show_menu_reason(HK_APP_STOP_CALLBACK_FAILED);
        }
        else if(hk_screen_get() == SCREEN_APP_SLOT_0 &&
                !app_runtime_integration_active())
            shell_show_menu_reason(HK_APP_STOP_COMPLETED);
        app = hk_app_for_screen(hk_screen_get());
        entry = hk_app_legacy_entry(app);
        if(hk_screen_get() == SCREEN_APP_SLOT_0 &&
           app_runtime_integration_active())
            tick_interval_us =
                app_runtime_integration_poll_interval_us(now_us);
        else
            tick_interval_us = app && entry &&
                               entry->screen == hk_screen_get() &&
                               app->limits.tick_interval_us ?
                               app->limits.tick_interval_us : 20000U;
        next_dispatch_us = now_us + tick_interval_us;
        sleep_us = tick_interval_us;
        if(sleep_us > HK_INPUT_SAMPLE_INTERVAL_US)
            sleep_us = HK_INPUT_SAMPLE_INTERVAL_US;
        hal_sleep_ms((uint32_t)((sleep_us + 999U) / 1000U));
    }

    return 0;
}
