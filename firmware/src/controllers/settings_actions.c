#include "settings_actions.h"

#include <stdio.h>

#include "../config/settings_config.h"

#include "../services/settings_lights.h"
#include "../services/settings_persistence.h"
#include "../services/settings_service.h"
#include "../core/hk_screen.h"

uint8_t settings_action_is_editable_row(uint8_t index)
{
    return index == SETTINGS_LED_BRIGHTNESS ||
           index == SETTINGS_RGB_RED ||
           index == SETTINGS_RGB_GREEN ||
           index == SETTINGS_RGB_BLUE ||
           index == SETTINGS_SCREEN_BRIGHTNESS ||
           index == SETTINGS_AUTO_SLEEP;
}

uint8_t settings_action_toggle_led(void)
{
    settings_set_led_enabled(settings_led_enabled() ? 0 : 1);
    illum_led_apply();
    settings_mark_dirty(1);
    printf("[LED] illum on=%u brightness=%u\r\n", settings_led_enabled(), settings_led_brightness());
    return SETTINGS_LED_SWITCH;
}

uint8_t settings_action_toggle_rgb(void)
{
    settings_set_rgb_enabled(settings_rgb_enabled() ? 0 : 1);
    rgb_led_apply();
    settings_mark_dirty(1);
    printf("[RGB] on=%u r=%u g=%u b=%u\r\n",
           settings_rgb_enabled(), settings_rgb_red(), settings_rgb_green(), settings_rgb_blue());
    return SETTINGS_RGB_SWITCH;
}

static uint8_t settings_action_adjust_rgb(uint8_t index, int8_t delta)
{
    int16_t next;
    uint8_t rgb_value;

    if(index == SETTINGS_RGB_RED)
        rgb_value = settings_rgb_red();
    else if(index == SETTINGS_RGB_GREEN)
        rgb_value = settings_rgb_green();
    else
        rgb_value = settings_rgb_blue();

    next = (int16_t)rgb_value + (int16_t)delta * 10;
    if(next < 0)
        next = 0;
    if(next > 100)
        next = 100;
    if((uint8_t)next == rgb_value)
        return SETTINGS_ACTION_NO_ROW;

    if(index == SETTINGS_RGB_RED)
        settings_set_rgb_red((uint8_t)next);
    else if(index == SETTINGS_RGB_GREEN)
        settings_set_rgb_green((uint8_t)next);
    else
        settings_set_rgb_blue((uint8_t)next);

    rgb_led_apply();
    settings_mark_dirty(0);
    printf("[RGB] on=%u r=%u g=%u b=%u\r\n",
           settings_rgb_enabled(), settings_rgb_red(), settings_rgb_green(), settings_rgb_blue());
    return index;
}

uint8_t settings_action_adjust_value(uint8_t index, int8_t delta)
{
    int16_t next;

    if(index == SETTINGS_LED_BRIGHTNESS)
    {
        next = (int16_t)settings_led_brightness() + (int16_t)delta * 10;
        if(next < 0)
            next = 0;
        if(next > 100)
            next = 100;
        if((uint8_t)next == settings_led_brightness())
            return SETTINGS_ACTION_NO_ROW;

        settings_set_led_brightness((uint8_t)next);
        illum_led_apply();
        settings_mark_dirty(0);
        printf("[LED] illum on=%u brightness=%u\r\n", settings_led_enabled(), settings_led_brightness());
        return SETTINGS_LED_BRIGHTNESS;
    }

    if(index == SETTINGS_RGB_RED || index == SETTINGS_RGB_GREEN || index == SETTINGS_RGB_BLUE)
        return settings_action_adjust_rgb(index, delta);

    if(index == SETTINGS_SCREEN_BRIGHTNESS)
    {
        next = (int16_t)settings_screen_brightness() + (int16_t)delta * 10;
        if(next < 10)
            next = 10;
        if(next > 100)
            next = 100;
        if((uint8_t)next == settings_screen_brightness())
            return SETTINGS_ACTION_NO_ROW;

        settings_set_screen_brightness((uint8_t)next);
        screen_brightness_apply();
        settings_mark_dirty(0);
        printf("[LCD] brightness=%u\r\n", settings_screen_brightness());
        return SETTINGS_SCREEN_BRIGHTNESS;
    }

    if(index == SETTINGS_AUTO_SLEEP)
    {
        next = (int16_t)settings_auto_sleep_minutes() + (int16_t)delta;
        if(next < 1)
            next = 1;
        if(next > 30)
            next = 30;
        if((uint8_t)next == settings_auto_sleep_minutes())
            return SETTINGS_ACTION_NO_ROW;

        settings_set_auto_sleep_minutes((uint8_t)next);
        activity_note();
        settings_mark_dirty(0);
        printf("[SETTINGS] auto_sleep=%u min\r\n", settings_auto_sleep_minutes());
        return SETTINGS_AUTO_SLEEP;
    }

    return SETTINGS_ACTION_NO_ROW;
}
