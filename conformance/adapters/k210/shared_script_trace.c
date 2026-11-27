#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <hackylens/capability/lights.h>

#include "k210_shared_script_fixture.h"
#include "lights_normative_backend.h"
#include "micropython_capability_bridge.h"

static uint8_t try_second_request;
static uint32_t second_attempts;
static uint32_t second_effects;
static micropython_binding_result_t second_result;

static micropython_binding_result_t script_call(micropython_binding_op_t op,
                                                const uint32_t args[6])
{
    size_t produced = 0;
    return micropython_binding_call(op, args, NULL, 0, NULL, 0, &produced);
}

void hk_test_k210_script_before_dispatch(void)
{
    if(!try_second_request)
        return;
    try_second_request = 0;
    second_attempts++;
    uint32_t effects = lights_normative_backend_effect_count();
    const uint32_t alternative[6] = {17, 0, 0, 0, 0, 0};
    second_result = script_call(MICROPYTHON_BINDING_OP_LED, alternative);
    second_effects += lights_normative_backend_effect_count() - effects;
}

static const char *native_outcome(hk_result_t code)
{
    switch(code)
    {
    case HK_OK: return "success";
    case HK_ERR_BUSY: return "conflict";
    case HK_ERR_INVALID_ARGUMENT: return "invalid_argument";
    default: return "unexpected_result";
    }
}

static const char *script_outcome(micropython_binding_result_t code)
{
    switch(code)
    {
    case MICROPYTHON_BINDING_OK: return "success";
    case MICROPYTHON_BINDING_ERROR_BUSY: return "conflict";
    case MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT: return "invalid_argument";
    case MICROPYTHON_BINDING_ERROR_TIMEOUT: return "deadline";
    case MICROPYTHON_BINDING_ERROR_NOT_ACTIVE: return "inactive";
    default: return "unexpected_result";
    }
}

int hk_test_k210_script_trace(int argc, char **argv)
{
    hk_lights_t led = {0};
    hk_lights_t rgb = {0};
    uint32_t run_id = 0;
    const uint32_t scalar[6] = {63, 0, 0, 0, 0, 0};
    const uint32_t color[6] = {255, 128, 0, 0, 0, 0};
    const uint32_t invalid[6] = {101, 0, 0, 0, 0, 0};
    lights_normative_backend_reset(0);
    const hk_lights_service_t *service = hk_lights_service();

    for(int i = 1; i < argc; ++i)
    {
        const char *action = argv[i];
        const char *result = "success";
        if(strcmp(action, "native-claim-led") == 0)
            result = native_outcome(hk_lights_open(service, HK_LIGHTS_CHANNEL_ILLUMINATION, &led));
        else if(strcmp(action, "native-claim-rgb") == 0)
            result = native_outcome(hk_lights_open(service, HK_LIGHTS_CHANNEL_RGB, &rgb));
        else if(strcmp(action, "native-led") == 0)
            result = native_outcome(hk_lights_set_level(&led, HK_LIGHTS_CHANNEL_ILLUMINATION, 630,
                                                       HK_DEADLINE_IMMEDIATE, NULL));
        else if(strcmp(action, "native-invalid-led") == 0)
            result = native_outcome(hk_lights_set_level(&led, HK_LIGHTS_CHANNEL_ILLUMINATION, 1010,
                                                       HK_DEADLINE_IMMEDIATE, NULL));
        else if(strcmp(action, "native-rgb") == 0)
            result = native_outcome(hk_lights_set_rgb(&rgb, 1000, 502, 0, HK_DEADLINE_IMMEDIATE, NULL));
        else if(strcmp(action, "native-release-led") == 0)
            result = native_outcome(hk_lights_close(&led, HK_DEADLINE_IMMEDIATE));
        else if(strcmp(action, "native-release-rgb") == 0)
            result = native_outcome(hk_lights_close(&rgb, HK_DEADLINE_IMMEDIATE));
        else if(strcmp(action, "script-prepare") == 0)
            run_id = hk_test_k210_script_prepare();
        else if(strcmp(action, "script-cleanup") == 0)
        {
            result = native_outcome(micropython_capability_bridge_cleanup());
            run_id = 0;
        }
        else if(strcmp(action, "script-led") == 0)
        {
            hk_test_k210_script_schedule(HK_TEST_SCRIPT_DISPATCH);
            result = script_outcome(script_call(MICROPYTHON_BINDING_OP_LED, scalar));
        }
        else if(strcmp(action, "script-invalid-led") == 0)
            result = script_outcome(script_call(MICROPYTHON_BINDING_OP_LED, invalid));
        else if(strcmp(action, "script-rgb") == 0)
            result = script_outcome(script_call(MICROPYTHON_BINDING_OP_RGB, color));
        else if(strcmp(action, "script-contended-led") == 0)
        {
            try_second_request = 1;
            result = script_outcome(script_call(MICROPYTHON_BINDING_OP_LED, scalar));
            try_second_request = 0;
        }
        else if(strcmp(action, "script-expiry-led") == 0)
        {
            hk_test_k210_script_schedule(HK_TEST_SCRIPT_TIMEOUT_BEFORE_DISPATCH);
            result = script_outcome(script_call(MICROPYTHON_BINDING_OP_LED, scalar));
        }
        else if(strcmp(action, "script-stop-led") == 0)
        {
            hk_test_k210_script_schedule(HK_TEST_SCRIPT_STOP_BEFORE_DISPATCH);
            result = script_outcome(script_call(MICROPYTHON_BINDING_OP_LED, scalar));
        }
        else if(strcmp(action, "consumer-tick") == 0)
            micropython_capability_bridge_tick();
        else
            return 2;

        uint16_t red, green, blue;
        lights_normative_backend_rgb(&red, &green, &blue);
        printf("{\"event\":\"shared_script\",\"action\":\"%s\",\"outcome\":\"%s\","
               "\"effects\":%" PRIu32 ",\"led\":%u,\"rgb\":[%u,%u,%u],\"run\":%" PRIu32 ","
               "\"second_attempts\":%" PRIu32 ",\"second_outcome\":\"%s\",\"second_effects\":%" PRIu32 ","
               "\"acknowledged\":%s,\"unwind_calls\":%" PRIu32 ",\"early_unwinds\":%" PRIu32 "}\n",
               action, result, lights_normative_backend_effect_count(),
               (unsigned)lights_normative_backend_level(HK_LIGHTS_CHANNEL_ILLUMINATION),
               (unsigned)red, (unsigned)green, (unsigned)blue, run_id, second_attempts,
               second_attempts ? script_outcome(second_result) : "absent", second_effects,
               micropython_capability_bridge_test_cancel_acknowledged() ? "true" : "false",
               hk_test_k210_script_unwind_calls(), hk_test_k210_script_unwind_before_ack());
    }
    return 0;
}
