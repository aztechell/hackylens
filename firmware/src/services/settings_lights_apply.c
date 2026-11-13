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

void settings_backlight_preview(uint8_t percent)
{
    if(settings_light_acquire(&s_backlight) == HK_OK)
        (void)hk_lights_set_level(&s_backlight.handle, HK_LIGHTS_CHANNEL_BACKLIGHT,
            (uint16_t)percent * 10U, HK_DEADLINE_IMMEDIATE, NULL);
}

void screen_brightness_apply(void)
{
    settings_backlight_preview(settings_screen_brightness());
}

void screen_brightness_off(void)
{
    settings_backlight_preview(0U);
}

void settings_illumination_preview(uint8_t percent)
{
    if(settings_light_acquire(&s_illumination) == HK_OK)
        (void)hk_lights_set_level(&s_illumination.handle, HK_LIGHTS_CHANNEL_ILLUMINATION,
            (uint16_t)percent * 10U, HK_DEADLINE_IMMEDIATE, NULL);
}

void illum_led_apply(void)
{
    settings_illumination_preview(settings_led_enabled() ? settings_led_brightness() : 0U);
}

void settings_rgb_preview(uint8_t red, uint8_t green, uint8_t blue)
{
    if(settings_light_acquire(&s_rgb) == HK_OK)
        (void)hk_lights_set_rgb(&s_rgb.handle, (uint16_t)red * 10U,
            (uint16_t)green * 10U, (uint16_t)blue * 10U, HK_DEADLINE_IMMEDIATE, NULL);
}

void rgb_led_apply(void)
{
    if(settings_rgb_enabled())
        settings_rgb_preview(settings_rgb_red(), settings_rgb_green(), settings_rgb_blue());
    else
        settings_rgb_preview(0U, 0U, 0U);
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
