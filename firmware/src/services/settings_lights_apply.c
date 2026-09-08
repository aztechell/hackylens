#include "settings_lights.h"

#include <stddef.h>

#include <hackylens/capability/lights.h>

#include "settings_service.h"

typedef struct
{
    uint32_t channel;
    hk_lights_t handle;
} settings_light_session_t;

static settings_light_session_t s_backlight = {HK_LIGHTS_CHANNEL_BACKLIGHT, {0}};
static settings_light_session_t s_illumination = {HK_LIGHTS_CHANNEL_ILLUMINATION, {0}};
static settings_light_session_t s_rgb = {HK_LIGHTS_CHANNEL_RGB, {0}};

static hk_result_t settings_light_acquire(settings_light_session_t *light)
{
    if(light->handle.service)
        return HK_OK;
    return hk_lights_open(hk_lights_service(), light->channel, &light->handle);
}

static void settings_light_release(settings_light_session_t *light)
{
    (void)hk_lights_close(&light->handle, HK_DEADLINE_IMMEDIATE);
}

void screen_brightness_apply(void)
{
    settings_set_screen_brightness(settings_screen_brightness());
    if(settings_light_acquire(&s_backlight) == HK_OK)
        (void)hk_lights_set_level(
            &s_backlight.handle,
            HK_LIGHTS_CHANNEL_BACKLIGHT,
            (uint16_t)settings_screen_brightness() * 10U,
            HK_DEADLINE_IMMEDIATE, NULL);
}

void screen_brightness_off(void)
{
    if(settings_light_acquire(&s_backlight) == HK_OK)
        (void)hk_lights_set_level(
            &s_backlight.handle,
            HK_LIGHTS_CHANNEL_BACKLIGHT, 0U,
            HK_DEADLINE_IMMEDIATE, NULL);
}

void illum_led_apply(void)
{
    uint16_t level;

    settings_set_led_brightness(settings_led_brightness());
    level = settings_led_enabled() ?
            (uint16_t)settings_led_brightness() * 10U : 0U;
    if(settings_light_acquire(&s_illumination) == HK_OK)
        (void)hk_lights_set_level(
            &s_illumination.handle,
            HK_LIGHTS_CHANNEL_ILLUMINATION, level,
            HK_DEADLINE_IMMEDIATE, NULL);
}

void rgb_led_apply(void)
{
    uint16_t red;
    uint16_t green;
    uint16_t blue;

    settings_set_rgb_red(settings_rgb_red());
    settings_set_rgb_green(settings_rgb_green());
    settings_set_rgb_blue(settings_rgb_blue());
    red = settings_rgb_enabled() ? (uint16_t)settings_rgb_red() * 10U : 0U;
    green = settings_rgb_enabled() ?
            (uint16_t)settings_rgb_green() * 10U : 0U;
    blue = settings_rgb_enabled() ? (uint16_t)settings_rgb_blue() * 10U : 0U;
    if(settings_light_acquire(&s_rgb) == HK_OK)
        (void)hk_lights_set_rgb(
            &s_rgb.handle, red, green, blue,
            HK_DEADLINE_IMMEDIATE, NULL);
}

void settings_lights_suspend(uint32_t channels)
{
    if(channels & HK_LIGHTS_CHANNEL_BACKLIGHT)
        settings_light_release(&s_backlight);
    if(channels & HK_LIGHTS_CHANNEL_ILLUMINATION)
        settings_light_release(&s_illumination);
    if(channels & HK_LIGHTS_CHANNEL_RGB)
        settings_light_release(&s_rgb);
}

void settings_lights_restore(uint32_t channels)
{
    if(channels & HK_LIGHTS_CHANNEL_BACKLIGHT)
        screen_brightness_apply();
    if(channels & HK_LIGHTS_CHANNEL_ILLUMINATION)
        illum_led_apply();
    if(channels & HK_LIGHTS_CHANNEL_RGB)
        rgb_led_apply();
}
