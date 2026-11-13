#ifndef SETTINGS_LIGHTS_H
#define SETTINGS_LIGHTS_H

#include <stdint.h>

void screen_brightness_apply(void);
void settings_backlight_preview(uint8_t percent);
void settings_illumination_preview(uint8_t percent);
void settings_rgb_preview(uint8_t red, uint8_t green, uint8_t blue);
void screen_brightness_off(void);
void illum_led_apply(void);
void rgb_led_apply(void);
void settings_lights_suspend(uint32_t channels);
void settings_lights_restore(uint32_t channels);

#endif
