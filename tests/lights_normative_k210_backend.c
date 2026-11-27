#include "lights_normative_backend.h"

#include <hackylens/capability/lights.h>



static uint64_t s_now_us;
static uint32_t s_effect_count;
static uint32_t s_active_mask;
static uint32_t s_safe_off_mask;
static uint16_t s_backlight_level;
static uint16_t s_illumination_level;
static uint16_t s_rgb_levels[3];

uint64_t hal_time_us(void)
{
    return s_now_us;
}

void lights_driver_prepare(void)
{
}

void lights_screen_backlight_set(uint8_t percent)
{
    s_effect_count++;
    s_backlight_level = (uint16_t)percent * 10U;
    if(percent != 0U)
        s_active_mask |= HK_LIGHTS_CHANNEL_BACKLIGHT;
    else
        s_active_mask &= ~HK_LIGHTS_CHANNEL_BACKLIGHT;
}

void lights_screen_backlight_off(void)
{
    s_effect_count++;
    s_backlight_level = 0;
    s_active_mask &= ~HK_LIGHTS_CHANNEL_BACKLIGHT;
    s_safe_off_mask |= HK_LIGHTS_CHANNEL_BACKLIGHT;
}

void lights_illum_set(uint8_t enabled, uint8_t brightness)
{
    s_effect_count++;
    s_illumination_level = enabled ? (uint16_t)brightness * 10U : 0;
    if(enabled)
        s_active_mask |= HK_LIGHTS_CHANNEL_ILLUMINATION;
    else
    {
        s_active_mask &= ~HK_LIGHTS_CHANNEL_ILLUMINATION;
        s_safe_off_mask |= HK_LIGHTS_CHANNEL_ILLUMINATION;
    }
}

void lights_rgb_set(
    uint8_t enabled, uint8_t red, uint8_t green, uint8_t blue)
{
    s_effect_count++;
    s_rgb_levels[0] = enabled ? (uint16_t)red * 10U : 0;
    s_rgb_levels[1] = enabled ? (uint16_t)green * 10U : 0;
    s_rgb_levels[2] = enabled ? (uint16_t)blue * 10U : 0;
    if(enabled)
        s_active_mask |= HK_LIGHTS_CHANNEL_RGB;
    else
    {
        s_active_mask &= ~HK_LIGHTS_CHANNEL_RGB;
        s_safe_off_mask |= HK_LIGHTS_CHANNEL_RGB;
    }
}

const char *lights_normative_backend_name(void)
{
    return "k210";
}

void lights_normative_backend_reset(uint64_t now_us)
{
    s_now_us = now_us;
    s_effect_count = 0U;
    s_active_mask = 0U;
    s_safe_off_mask = 0U;
    s_backlight_level = 0;
    s_illumination_level = 0;
    s_rgb_levels[0] = 0;
    s_rgb_levels[1] = 0;
    s_rgb_levels[2] = 0;
}

void lights_normative_backend_set_now(uint64_t now_us)
{
    s_now_us = now_us;
}

uint32_t lights_normative_backend_effect_count(void)
{
    return s_effect_count;
}

uint32_t lights_normative_backend_active_mask(void)
{
    return s_active_mask;
}

uint32_t lights_normative_backend_safe_off_mask(void)
{
    return s_safe_off_mask;
}

uint16_t lights_normative_backend_level(uint32_t channel)
{
    if(channel == HK_LIGHTS_CHANNEL_BACKLIGHT)
        return s_backlight_level;
    if(channel == HK_LIGHTS_CHANNEL_ILLUMINATION)
        return s_illumination_level;
    return 0;
}

void lights_normative_backend_rgb(uint16_t *red, uint16_t *green, uint16_t *blue)
{
    *red = s_rgb_levels[0];
    *green = s_rgb_levels[1];
    *blue = s_rgb_levels[2];
}
