#ifndef SETTINGS_LIGHTS_H
#define SETTINGS_LIGHTS_H

#include <stdint.h>

#include "../core/camera_types.h"

void screen_brightness_apply(void);
void screen_brightness_off(void);
void illum_led_apply(void);
void rgb_led_apply(void);
const char *camera_light_mode_label(camera_light_mode_t mode);
void camera_light_outputs_off(void);
void camera_light_apply(void);
void camera_light_restore_global(void);
void camera_light_adjust(int8_t delta);

#endif
