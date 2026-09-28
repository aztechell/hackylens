#define APP_RUNTIME_PRODUCTION_REAL_SETTINGS_LIGHTS
#define main production_fixture_main
#include "app_runtime_production_harness.c"
#undef main

#include "lights_normative_backend.h"
#include "../firmware/src/capabilities/lights_provider.h"
#include "../firmware/src/services/settings_lights.h"
#include "../firmware/src/services/settings_service.h"

static uint8_t s_brightness = 40U;
static uint8_t s_illumination = 50U;
static uint8_t s_led_enabled = 1U;
static uint8_t s_rgb_enabled = 1U;
static uint8_t s_red = 10U, s_green = 20U, s_blue = 30U;
static uint32_t s_requested_channel;
static uint8_t s_fail_after_claim;
static hk_lights_t *s_app_light;
static const hk_lights_t *s_settings_claimants[3];

uint8_t settings_screen_brightness(void) { return s_brightness; }
void settings_set_screen_brightness(uint8_t value) { s_brightness = value; }
uint8_t settings_led_brightness(void) { return s_illumination; }
void settings_set_led_brightness(uint8_t value) { s_illumination = value; }
uint8_t settings_led_enabled(void) { return s_led_enabled; }
uint8_t settings_rgb_enabled(void) { return s_rgb_enabled; }
uint8_t settings_rgb_red(void) { return s_red; }
uint8_t settings_rgb_green(void) { return s_green; }
uint8_t settings_rgb_blue(void) { return s_blue; }
void settings_set_rgb_red(uint8_t value) { s_red = value; }
void settings_set_rgb_green(uint8_t value) { s_green = value; }
void settings_set_rgb_blue(uint8_t value) { s_blue = value; }

static hk_result_t lights_test_start(const hk_app_context_t *ctx)
{
    hk_lights_t *duplicate = NULL;
    hk_result_t result = hk_app_context_lights(ctx, s_requested_channel, &s_app_light);
    if(result != HK_OK)
        return result;
    if(hk_app_context_lights(ctx, s_requested_channel, &duplicate) != HK_OK ||
       duplicate != s_app_light)
        return HK_ERR_INTERNAL;
    for(unsigned i = 0U; i < 3U; ++i)
    {
        const hk_lights_t *claimant = hk_lights_binding.state->claimants[i];
        if(s_requested_channel & (1U << i))
        {
            if(claimant != s_app_light)
                return HK_ERR_INTERNAL;
        }
        else if(claimant != s_settings_claimants[i])
            return HK_ERR_INTERNAL;
    }
    return s_fail_after_claim ? HK_ERR_INTERNAL : HK_OK;
}

static hk_result_t lights_test_stop(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}

static const hk_app_v2_entry_t s_lights_entry = {
    .state_storage = s_state,
    .state_capacity_bytes = sizeof(s_state),
    .start = lights_test_start,
    .event = app_event,
    .stop = lights_test_stop,
};
static const hk_app_t s_lights_app = {
    .struct_size = sizeof(hk_app_t),
    .struct_version = HK_APP_DESCRIPTOR_VERSION,
    .id = "production-lights-test",
    .title = "Production lights test",
    .entry = &s_lights_entry,
    .limits = {sizeof(s_state), 256U, sizeof(s_state),
        HK_APP_STATE_ALIGNMENT, 100U, 50U, 50U},
};

static int settings_owns_all(void)
{
    for(unsigned i = 0U; i < 3U; ++i)
        if(!hk_lights_binding.state->claimants[i] ||
           hk_lights_binding.state->claimants[i] == s_app_light)
            return 0;
    return 1;
}

int main(void)
{
    uint16_t red, green, blue;
    const uint32_t channels[3] = {
        HK_LIGHTS_CHANNEL_BACKLIGHT,
        HK_LIGHTS_CHANNEL_ILLUMINATION,
        HK_LIGHTS_CHANNEL_RGB,
    };

    s_fixture.now_us = 1000U;
    lights_normative_backend_reset(1000U);
    screen_brightness_apply();
    illum_led_apply();
    rgb_led_apply();
    CHECK(settings_owns_all());
    for(unsigned i = 0U; i < 3U; ++i)
        s_settings_claimants[i] = hk_lights_binding.state->claimants[i];
    CHECK(app_runtime_integration_initialize() == HK_OK);

    for(unsigned i = 0U; i < 3U; ++i)
    {
        s_requested_channel = channels[i];
        s_app_light = NULL;
        CHECK(app_runtime_integration_open(&s_lights_app, NULL) == HK_OK);
        CHECK(s_app_light != NULL);
        s_brightness = 61U;
        s_illumination = 72U;
        s_red = 13U; s_green = 24U; s_blue = 35U;
        CHECK(app_runtime_integration_close(HK_APP_STOP_BACK) == HK_OK);
        CHECK(settings_owns_all());
        if(i == 0U)
            CHECK(lights_normative_backend_level(HK_LIGHTS_CHANNEL_BACKLIGHT) ==
                (uint16_t)s_brightness * 10U);
        if(i == 1U)
            CHECK(lights_normative_backend_level(HK_LIGHTS_CHANNEL_ILLUMINATION) ==
                (uint16_t)s_illumination * 10U);
        if(i == 2U)
        {
            lights_normative_backend_rgb(&red, &green, &blue);
            CHECK(red == (uint16_t)s_red * 10U &&
                  green == (uint16_t)s_green * 10U &&
                  blue == (uint16_t)s_blue * 10U);
        }
    }

    s_requested_channel = HK_LIGHTS_CHANNEL_BACKLIGHT;
    s_app_light = NULL;
    lights_normative_backend_fail_next_info();
    CHECK(app_runtime_integration_open(&s_lights_app, NULL) == HK_ERR_BUSY);
    CHECK(app_runtime_integration_active() == NULL);
    CHECK(settings_owns_all());

    s_requested_channel = HK_LIGHTS_CHANNEL_RGB;
    s_app_light = NULL;
    s_fail_after_claim = 1U;
    CHECK(app_runtime_integration_open(&s_lights_app, NULL) == HK_ERR_INTERNAL);
    CHECK(app_runtime_integration_active() == NULL);
    CHECK(settings_owns_all());
    puts("APP_RUNTIME_LIGHTS_OK channels=3 duplicate=1 restore=1 failure=1");
    return 0;
}
