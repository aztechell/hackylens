#include "camera_light.h"

#include <stdio.h>

#include <hackylens/capability/lights.h>

#include "camera_session_preferences.h"
#include "settings_lights.h"
#include "../core/hk_binary.h"

#define CAMERA_LIGHT_CHANNELS \
    (HK_LIGHTS_CHANNEL_ILLUMINATION | HK_LIGHTS_CHANNEL_RGB)

static hk_lights_t s_camera_lights;
static hk_result_t camera_lights_acquire(void)
{
    hk_result_t result;
    if(s_camera_lights.service)
        return HK_OK;
    settings_lights_suspend(CAMERA_LIGHT_CHANNELS);
    result = hk_lights_open(
        hk_lights_service(), CAMERA_LIGHT_CHANNELS, &s_camera_lights);
    if(result != HK_OK)
        settings_lights_restore(CAMERA_LIGHT_CHANNELS);
    return result;
}

const char *camera_light_mode_label(camera_light_mode_t mode)
{
    return mode == CAMERA_LIGHT_RGB ? "RGB" : "LED";
}

void camera_light_outputs_off(void)
{
    if(camera_lights_acquire() != HK_OK)
        return;
    (void)hk_lights_set_level(
        &s_camera_lights,
        HK_LIGHTS_CHANNEL_ILLUMINATION, 0U,
        HK_DEADLINE_IMMEDIATE, NULL);
    (void)hk_lights_set_rgb(
        &s_camera_lights, 0U, 0U, 0U,
        HK_DEADLINE_IMMEDIATE, NULL);
}

void camera_light_apply(void)
{
    uint8_t level = clamp_u8(camera_service_light_level(), 0U, 100U);

    camera_service_set_light_level(level);
    if(camera_lights_acquire() != HK_OK)
        return;
    if(level == 0U)
    {
        camera_light_outputs_off();
        return;
    }
    if(camera_session_preferences_light_mode() == CAMERA_LIGHT_RGB)
    {
        (void)hk_lights_set_level(
            &s_camera_lights,
            HK_LIGHTS_CHANNEL_ILLUMINATION, 0U,
            HK_DEADLINE_IMMEDIATE, NULL);
        (void)hk_lights_set_rgb(
            &s_camera_lights,
            (uint16_t)((uint16_t)level *
                       camera_session_preferences_rgb_red() / 10U),
            (uint16_t)((uint16_t)level *
                       camera_session_preferences_rgb_green() / 10U),
            (uint16_t)((uint16_t)level *
                       camera_session_preferences_rgb_blue() / 10U),
            HK_DEADLINE_IMMEDIATE, NULL);
        return;
    }
    (void)hk_lights_set_rgb(
        &s_camera_lights, 0U, 0U, 0U,
        HK_DEADLINE_IMMEDIATE, NULL);
    (void)hk_lights_set_level(
        &s_camera_lights,
        HK_LIGHTS_CHANNEL_ILLUMINATION, (uint16_t)level * 10U,
        HK_DEADLINE_IMMEDIATE, NULL);
}

hk_result_t camera_light_retire(hk_deadline_t deadline)
{
    uint8_t had_session = s_camera_lights.service != NULL;
    hk_result_t result = hk_lights_retire(&s_camera_lights, deadline);
    camera_service_set_light_active(0U);
    camera_service_set_light_level(0U);
    if(had_session)
        settings_lights_restore(CAMERA_LIGHT_CHANNELS);
    return result;
}

void camera_light_restore_global(void)
{
    (void)camera_light_retire(HK_DEADLINE_IMMEDIATE);
}

void camera_light_adjust(int8_t delta)
{
    int16_t next = (int16_t)camera_service_light_level() +
                   (int16_t)delta * 10;

    if(next < 0)
        next = 0;
    if(next > 100)
        next = 100;
    if((uint8_t)next == camera_service_light_level())
        return;
    camera_service_set_light_level((uint8_t)next);
    camera_light_apply();
    printf("[CAM] light mode=%s level=%u rgb=%u/%u/%u\r\n",
           camera_light_mode_label(camera_session_preferences_light_mode()),
           camera_service_light_level(),
           camera_session_preferences_rgb_red(),
           camera_session_preferences_rgb_green(),
           camera_session_preferences_rgb_blue());
}
