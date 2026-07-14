#include "settings_service.h"

#include <stdio.h>

#include "camera_light.h"

#include "../core/camera_types.h"
#include "../core/hk_binary.h"
#include "../drivers/hk_lights.h"
#include "hk_config.h"
#if HK_ENABLE_CAMERA_FEATURE
#endif

void screen_brightness_apply(void)
{
    settings_set_screen_brightness(settings_screen_brightness());
    lights_screen_backlight_set(settings_screen_brightness());
}

void screen_brightness_off(void)
{
    lights_screen_backlight_off();
}

void illum_led_apply(void)
{
    settings_set_led_brightness(settings_led_brightness());
    lights_illum_set(settings_led_enabled(), settings_led_brightness());
}

void rgb_led_apply(void)
{
    settings_set_rgb_red(settings_rgb_red());
    settings_set_rgb_green(settings_rgb_green());
    settings_set_rgb_blue(settings_rgb_blue());
    lights_rgb_set(settings_rgb_enabled(), settings_rgb_red(), settings_rgb_green(), settings_rgb_blue());
}

const char *camera_light_mode_label(camera_light_mode_t mode)
{
    return mode == CAMERA_LIGHT_RGB ? "RGB" : "LED";
}

#if HK_ENABLE_CAMERA_FEATURE
void camera_light_outputs_off(void)
{
    lights_camera_outputs_off();
}

void camera_light_apply(void)
{
    uint8_t level = clamp_u8(camera_service_light_level(), 0, 100);

    camera_service_set_light_level(level);
    lights_camera_set(camera_service_light_mode(),
                      level,
                      camera_service_rgb_channel(CAMERA_RGB_RED),
                      camera_service_rgb_channel(CAMERA_RGB_GREEN),
                      camera_service_rgb_channel(CAMERA_RGB_BLUE));
}

void camera_light_restore_global(void)
{
    camera_service_set_light_active(0);
    camera_service_set_light_level(0);
    camera_light_outputs_off();
    illum_led_apply();
    rgb_led_apply();
}

void camera_light_adjust(int8_t delta)
{
    int16_t next = (int16_t)camera_service_light_level() + (int16_t)delta * 10;

    if(next < 0)
        next = 0;
    if(next > 100)
        next = 100;
    if((uint8_t)next == camera_service_light_level())
        return;

    camera_service_set_light_level((uint8_t)next);
    camera_light_apply();
    printf("[CAM] light mode=%s level=%u rgb=%u/%u/%u\r\n",
           camera_light_mode_label(camera_service_light_mode()),
           camera_service_light_level(),
           camera_service_rgb_channel(CAMERA_RGB_RED),
           camera_service_rgb_channel(CAMERA_RGB_GREEN),
           camera_service_rgb_channel(CAMERA_RGB_BLUE));
}
#endif
