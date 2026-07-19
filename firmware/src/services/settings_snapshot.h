#ifndef SETTINGS_SNAPSHOT_H
#define SETTINGS_SNAPSHOT_H

#include <stdint.h>

#include "hk_config.h"

#if HK_ENABLE_CAMERA_FEATURE
#include "camera_persist_settings.h"
#endif

typedef struct
{
    uint8_t led_enabled;
    uint8_t led_brightness;
    uint8_t rgb_enabled;
    uint8_t rgb_red;
    uint8_t rgb_green;
    uint8_t rgb_blue;
    uint8_t screen_brightness;
    uint8_t auto_sleep_minutes;
    uint8_t feature_flags;
    uint8_t external_link_uart_speed;
#if HK_ENABLE_CAMERA_FEATURE
    camera_persist_settings_t camera;
#endif
#if HK_ENABLE_QR_FEATURE
    uint8_t qr_decode_rate;
#endif
} settings_snapshot_t;

void settings_snapshot_capture(settings_snapshot_t *snapshot);
void settings_snapshot_apply(const settings_snapshot_t *snapshot);

#endif
