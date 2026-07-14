#ifndef SETTINGS_SERVICE_H
#define SETTINGS_SERVICE_H

#include <stdint.h>

void settings_defaults(void);
uint8_t settings_led_enabled(void);
uint8_t settings_led_brightness(void);
uint8_t settings_rgb_enabled(void);
uint8_t settings_rgb_red(void);
uint8_t settings_rgb_green(void);
uint8_t settings_rgb_blue(void);
uint8_t settings_screen_brightness(void);
uint8_t settings_auto_sleep_minutes(void);
uint8_t settings_feature_flags(void);
uint8_t hk_auto_sleep_minutes(void);
void settings_set_led_enabled(uint8_t enabled);
void settings_set_led_brightness(uint8_t brightness);
void settings_set_rgb_enabled(uint8_t enabled);
void settings_set_rgb_red(uint8_t red);
void settings_set_rgb_green(uint8_t green);
void settings_set_rgb_blue(uint8_t blue);
void settings_set_screen_brightness(uint8_t brightness);
void settings_set_auto_sleep_minutes(uint8_t minutes);
void settings_set_feature_flags(uint8_t flags);

#endif
