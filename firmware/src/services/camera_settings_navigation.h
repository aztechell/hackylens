#ifndef HK_CAMERA_SETTINGS_NAVIGATION_H
#define HK_CAMERA_SETTINGS_NAVIGATION_H

#include <stdint.h>

uint8_t camera_service_settings_qr_mode(void);
uint8_t camera_service_settings_index(void);
uint8_t camera_service_settings_top(void);
void camera_service_settings_begin(uint8_t qr_mode);
void camera_service_settings_set_index(uint8_t index);
void camera_service_settings_set_top(uint8_t top);

#endif
