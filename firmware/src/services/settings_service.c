#include "../core/hk_binary.h"

#include <string.h>

#include "camera_persist_settings.h"
#include "settings_snapshot.h"

#include "../config/camera_config.h"
#include "../config/settings_config.h"
#include "settings_service.h"
#include "hk_config.h"
#if HK_ENABLE_CAMERA_FEATURE
#endif
#if HK_ENABLE_QR_FEATURE
#include "qr_service.h"
#endif

static uint8_t g_led_enabled = 0;
static uint8_t g_led_brightness = 20;
static uint8_t g_rgb_enabled = 0;
static uint8_t g_rgb_red = 20;
static uint8_t g_rgb_green = 20;
static uint8_t g_rgb_blue = 20;
static uint8_t g_screen_brightness = 90;
static uint8_t g_auto_sleep_minutes = 1;
static uint8_t g_feature_flags;

uint8_t hk_auto_sleep_minutes(void)
{
    return g_auto_sleep_minutes;
}

uint8_t settings_led_enabled(void)
{
    return g_led_enabled;
}

uint8_t settings_led_brightness(void)
{
    return g_led_brightness;
}

uint8_t settings_rgb_enabled(void)
{
    return g_rgb_enabled;
}

uint8_t settings_rgb_red(void)
{
    return g_rgb_red;
}

uint8_t settings_rgb_green(void)
{
    return g_rgb_green;
}

uint8_t settings_rgb_blue(void)
{
    return g_rgb_blue;
}

uint8_t settings_screen_brightness(void)
{
    return g_screen_brightness;
}

uint8_t settings_auto_sleep_minutes(void)
{
    return g_auto_sleep_minutes;
}

uint8_t settings_feature_flags(void)
{
    return g_feature_flags;
}

void settings_set_led_enabled(uint8_t enabled)
{
    g_led_enabled = enabled ? 1 : 0;
}

void settings_set_led_brightness(uint8_t brightness)
{
    g_led_brightness = clamp_u8(brightness, 0, 100);
}

void settings_set_rgb_enabled(uint8_t enabled)
{
    g_rgb_enabled = enabled ? 1 : 0;
}

void settings_set_rgb_red(uint8_t red)
{
    g_rgb_red = clamp_u8(red, 0, 100);
}

void settings_set_rgb_green(uint8_t green)
{
    g_rgb_green = clamp_u8(green, 0, 100);
}

void settings_set_rgb_blue(uint8_t blue)
{
    g_rgb_blue = clamp_u8(blue, 0, 100);
}

void settings_set_screen_brightness(uint8_t brightness)
{
    g_screen_brightness = clamp_u8(brightness, 10, 100);
}

void settings_set_auto_sleep_minutes(uint8_t minutes)
{
    g_auto_sleep_minutes = clamp_u8(minutes, 1, 30);
}

void settings_set_feature_flags(uint8_t flags)
{
    g_feature_flags = flags & SETTINGS_FEATURE_FLAGS_MASK;
}

void settings_defaults(void)
{
    g_led_enabled = 0;
    g_led_brightness = 20;
    g_rgb_enabled = 0;
    g_rgb_red = 20;
    g_rgb_green = 20;
    g_rgb_blue = 20;
    g_screen_brightness = 90;
    g_auto_sleep_minutes = 1;
    g_feature_flags = 0;
#if HK_ENABLE_CAMERA_FEATURE
    camera_service_persist_defaults();
#endif
#if HK_ENABLE_QR_FEATURE
    qr_service_set_decode_rate(QR_DECODE_RATE_DEFAULT);
#endif
}

void settings_snapshot_capture(settings_snapshot_t *snapshot)
{
    if(!snapshot)
        return;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->led_enabled = g_led_enabled ? 1 : 0;
    snapshot->led_brightness = clamp_u8(g_led_brightness, 0, 100);
    snapshot->rgb_enabled = g_rgb_enabled ? 1 : 0;
    snapshot->rgb_red = clamp_u8(g_rgb_red, 0, 100);
    snapshot->rgb_green = clamp_u8(g_rgb_green, 0, 100);
    snapshot->rgb_blue = clamp_u8(g_rgb_blue, 0, 100);
    snapshot->screen_brightness = clamp_u8(g_screen_brightness, 10, 100);
    snapshot->auto_sleep_minutes = clamp_u8(g_auto_sleep_minutes, 1, 30);
    snapshot->feature_flags = g_feature_flags & SETTINGS_FEATURE_FLAGS_MASK;
#if HK_ENABLE_CAMERA_FEATURE
    camera_service_persist_get(&snapshot->camera);
#endif
#if HK_ENABLE_QR_FEATURE
    snapshot->qr_decode_rate = clamp_u8(qr_service_decode_rate(), QR_DECODE_RATE_MIN, QR_DECODE_RATE_MAX);
#endif
}

void settings_snapshot_apply(const settings_snapshot_t *snapshot)
{
    if(!snapshot)
        return;

    settings_set_led_enabled(snapshot->led_enabled);
    settings_set_led_brightness(snapshot->led_brightness);
    settings_set_rgb_enabled(snapshot->rgb_enabled);
    settings_set_rgb_red(snapshot->rgb_red);
    settings_set_rgb_green(snapshot->rgb_green);
    settings_set_rgb_blue(snapshot->rgb_blue);
    settings_set_screen_brightness(snapshot->screen_brightness);
    settings_set_auto_sleep_minutes(snapshot->auto_sleep_minutes);
    settings_set_feature_flags(snapshot->feature_flags);
#if HK_ENABLE_CAMERA_FEATURE
    camera_service_persist_apply(&snapshot->camera);
#endif
#if HK_ENABLE_QR_FEATURE
    qr_service_set_decode_rate(clamp_u8(snapshot->qr_decode_rate, QR_DECODE_RATE_MIN, QR_DECODE_RATE_MAX));
#endif
}
