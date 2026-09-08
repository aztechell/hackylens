#include <hackylens/capability/lights.h>

#include <stdio.h>

#include "../firmware/src/capabilities/capability_provider.h"
#include "../firmware/src/capabilities/lights_provider.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("K210_LIGHTS_FAIL line=%d\n", __LINE__);                \
            return 1;                                                        \
        }                                                                    \
    } while(0)



static uint64_t s_now_us = 100U;
static uint32_t s_writes;
static uint32_t s_backlight_off;
static uint8_t s_illum_enabled;
static uint8_t s_illum_percent;
static uint8_t s_red;
static uint8_t s_green;
static uint8_t s_blue;
static uint32_t s_prepare_calls;

uint64_t hal_time_us(void)
{
    return s_now_us;
}

void lights_driver_prepare(void)
{
    s_prepare_calls++;
}

void lights_screen_backlight_set(uint8_t percent)
{
    (void)percent;
    s_writes++;
}

void lights_screen_backlight_off(void)
{
    s_backlight_off++;
    s_writes++;
}

void lights_illum_set(uint8_t enabled, uint8_t brightness)
{
    s_illum_enabled = enabled;
    s_illum_percent = brightness;
    s_writes++;
}

void lights_rgb_set(
    uint8_t enabled, uint8_t red, uint8_t green, uint8_t blue)
{
    s_red = enabled ? red : 0U;
    s_green = enabled ? green : 0U;
    s_blue = enabled ? blue : 0U;
    s_writes++;
}

static uint8_t cancelled(const void *context)
{
    return *(const uint8_t *)context;
}

int main(void)
{
    const hk_lights_service_t *provider = hk_lights_service();
    hk_lights_t illumination = {0}, rgb = {0}, conflicting = {0};
    hk_lights_info_t info;
    uint8_t cancel_flag = 1U;
    hk_cancel_t cancel = {cancelled, &cancel_flag};
    uint32_t writes_before_cleanup;

    CHECK(provider != NULL);
    CHECK(hk_lights_get_info(provider, &info) == HK_OK);
    CHECK(info.supported_channels == HK_LIGHTS_CHANNEL_ALL &&
          info.maximum_level == 1000U);
    CHECK(hk_lights_open(provider, HK_LIGHTS_CHANNEL_ILLUMINATION, &illumination) == HK_OK);
    CHECK(hk_lights_open(provider, HK_LIGHTS_CHANNEL_RGB, &rgb) == HK_OK);
    CHECK(s_prepare_calls == 1U);
    CHECK(hk_lights_open(provider, HK_LIGHTS_CHANNEL_BACKLIGHT | HK_LIGHTS_CHANNEL_ILLUMINATION, &conflicting) ==
          HK_ERR_BUSY);

    CHECK(hk_lights_set_level( &illumination,
        HK_LIGHTS_CHANNEL_ILLUMINATION, 505U,
        HK_DEADLINE_IMMEDIATE, &cancel) == HK_ERR_CANCELLED);
    CHECK(hk_lights_set_level( &illumination,
        HK_LIGHTS_CHANNEL_ILLUMINATION, 505U,
        (hk_deadline_t){100U}, NULL) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(s_writes == 0U);
    cancel_flag = 0U;
    CHECK(hk_lights_set_level( &illumination,
        HK_LIGHTS_CHANNEL_ILLUMINATION, 505U,
        HK_DEADLINE_IMMEDIATE, &cancel) == HK_OK);
    CHECK(s_illum_enabled == 1U && s_illum_percent == 51U);
    CHECK(hk_lights_set_rgb( &rgb, 1000U, 500U, 1U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_OK);
    CHECK(s_red == 100U && s_green == 50U && s_blue == 0U);
    CHECK(hk_lights_set_level( &illumination,
        HK_LIGHTS_CHANNEL_BACKLIGHT, 500U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_WRONG_OWNER);

    writes_before_cleanup = s_writes;
    CHECK(hk_lights_close( &illumination, (hk_deadline_t){100U}) ==
          HK_ERR_DEADLINE_EXCEEDED);
    CHECK(s_writes == writes_before_cleanup && s_illum_enabled == 1U);
    CHECK(hk_lights_close( &illumination, HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(s_illum_enabled == 0U && s_illum_percent == 0U);
    writes_before_cleanup = s_writes;
    CHECK(hk_lights_close(&rgb,
        (hk_deadline_t){100U}) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(s_writes == writes_before_cleanup && s_red == 100U &&
          s_green == 50U && s_blue == 0U);
    CHECK(hk_lights_close(&rgb,
        HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(s_red == 0U && s_green == 0U && s_blue == 0U);
    CHECK(s_backlight_off == 0U);

    printf("K210_LIGHTS_OK writes=%u illum_percent=51 rgb=100/50/0\n",
           (unsigned)s_writes);
    return 0;
}
