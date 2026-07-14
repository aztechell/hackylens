#include "hk_lights.h"

#include "../core/camera_types.h"

#include "../board/board_pins.h"

#include "../core/hk_binary.h"
#include "../hal/hal_pwm.h"

void lights_screen_backlight_set(uint8_t percent)
{
    uint8_t duty = clamp_u8(percent, 10, 100);
    hal_pwm_set(SCREEN_BL_PWM_DEVICE, SCREEN_BL_PWM_CHANNEL, PWM_FREQ_HZ, (double)duty / 100.0);
    hal_pwm_enable(SCREEN_BL_PWM_DEVICE, SCREEN_BL_PWM_CHANNEL, 1);
}

void lights_screen_backlight_off(void)
{
    hal_pwm_enable(SCREEN_BL_PWM_DEVICE, SCREEN_BL_PWM_CHANNEL, 0);
}

void lights_illum_set(uint8_t enabled, uint8_t brightness)
{
    uint8_t duty = enabled ? clamp_u8(brightness, 0, 100) : 0;
    hal_pwm_set(LED_PWM_DEVICE, LED_PWM_CHANNEL, PWM_FREQ_HZ, (double)duty / 100.0);
    hal_pwm_enable(LED_PWM_DEVICE, LED_PWM_CHANNEL, enabled && duty > 0);
}

void lights_rgb_set(uint8_t enabled, uint8_t red, uint8_t green, uint8_t blue)
{
    uint8_t r = enabled ? clamp_u8(red, 0, 100) : 0;
    uint8_t g = enabled ? clamp_u8(green, 0, 100) : 0;
    uint8_t b = enabled ? clamp_u8(blue, 0, 100) : 0;

    hal_pwm_set(RGB_PWM_DEVICE, RGB_PWM_CHANNEL0, PWM_FREQ_HZ, (double)r / 100.0);
    hal_pwm_set(RGB_PWM_DEVICE, RGB_PWM_CHANNEL1, PWM_FREQ_HZ, (double)g / 100.0);
    hal_pwm_set(RGB_PWM_DEVICE, RGB_PWM_CHANNEL2, PWM_FREQ_HZ, (double)b / 100.0);
    hal_pwm_enable(RGB_PWM_DEVICE, RGB_PWM_CHANNEL0, enabled && r > 0);
    hal_pwm_enable(RGB_PWM_DEVICE, RGB_PWM_CHANNEL1, enabled && g > 0);
    hal_pwm_enable(RGB_PWM_DEVICE, RGB_PWM_CHANNEL2, enabled && b > 0);
}

void lights_camera_outputs_off(void)
{
    lights_illum_set(0, 0);
    lights_rgb_set(0, 0, 0, 0);
}

void lights_camera_set(camera_light_mode_t mode, uint8_t level, uint8_t red, uint8_t green, uint8_t blue)
{
    uint8_t duty = clamp_u8(level, 0, 100);
    double scale = (double)duty / 100.0;

    if(duty == 0)
    {
        lights_camera_outputs_off();
        return;
    }

    if(mode == CAMERA_LIGHT_RGB)
    {
        lights_illum_set(0, 0);
        hal_pwm_set(RGB_PWM_DEVICE, RGB_PWM_CHANNEL0, PWM_FREQ_HZ, scale * (double)clamp_u8(red, 0, 100) / 100.0);
        hal_pwm_set(RGB_PWM_DEVICE, RGB_PWM_CHANNEL1, PWM_FREQ_HZ, scale * (double)clamp_u8(green, 0, 100) / 100.0);
        hal_pwm_set(RGB_PWM_DEVICE, RGB_PWM_CHANNEL2, PWM_FREQ_HZ, scale * (double)clamp_u8(blue, 0, 100) / 100.0);
        hal_pwm_enable(RGB_PWM_DEVICE, RGB_PWM_CHANNEL0, red > 0);
        hal_pwm_enable(RGB_PWM_DEVICE, RGB_PWM_CHANNEL1, green > 0);
        hal_pwm_enable(RGB_PWM_DEVICE, RGB_PWM_CHANNEL2, blue > 0);
        return;
    }

    lights_rgb_set(0, 0, 0, 0);
    hal_pwm_set(LED_PWM_DEVICE, LED_PWM_CHANNEL, PWM_FREQ_HZ, scale);
    hal_pwm_enable(LED_PWM_DEVICE, LED_PWM_CHANNEL, 1);
}
