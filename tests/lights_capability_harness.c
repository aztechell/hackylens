#include <hackylens/capability/lights.h>

#include <stdio.h>
#include <string.h>

#include "lights_normative_backend.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("LIGHTS_NORMATIVE_FAIL backend=%s line=%d\n",          \
                   lights_normative_backend_name(), __LINE__);               \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static uint8_t cancelled(const void *context)
{
    return *(const uint8_t *)context;
}

int main(void)
{
    const hk_lights_service_t *service = hk_lights_service();
    hk_lights_t illumination = {0}, rgb = {0}, backlight = {0}, replacement = {0}, copy;
    hk_lights_info_t info;
    uint8_t flag = 1U;
    hk_cancel_t cancel = {cancelled, &flag};
    uint32_t effects;
    lights_normative_backend_reset(100U);
    CHECK(service != NULL);
    CHECK(hk_lights_get_info(service, &info) == HK_OK);
    CHECK(info.supported_channels == HK_LIGHTS_CHANNEL_ALL && info.maximum_level == 1000U);
    CHECK(hk_lights_open(NULL, HK_LIGHTS_CHANNEL_RGB, &replacement) == HK_ERR_CAPABILITY_ABSENT);
    CHECK(hk_lights_open(service, 0U, &replacement) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_lights_open(service, 0x80U, &replacement) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_lights_open(service, HK_LIGHTS_CHANNEL_ILLUMINATION, &illumination) == HK_OK);
    CHECK(hk_lights_open(service, HK_LIGHTS_CHANNEL_RGB, &rgb) == HK_OK);
    CHECK(hk_lights_open(service, HK_LIGHTS_CHANNEL_BACKLIGHT | HK_LIGHTS_CHANNEL_ILLUMINATION, &replacement) == HK_ERR_BUSY);
    CHECK(hk_lights_open(service, HK_LIGHTS_CHANNEL_BACKLIGHT, &backlight) == HK_OK);
    copy = illumination;
    CHECK(hk_lights_set_level(&copy, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U, HK_DEADLINE_IMMEDIATE, NULL) != HK_OK);
    CHECK(hk_lights_close(&copy, HK_DEADLINE_IMMEDIATE) != HK_OK);
    CHECK(hk_lights_set_level(&illumination, HK_LIGHTS_CHANNEL_BACKLIGHT, 500U, HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_WRONG_OWNER);
    CHECK(hk_lights_set_level(&illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 1001U, HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_lights_set_rgb(&rgb, 1001U, 0U, 0U, HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_lights_set_level(&illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U, HK_DEADLINE_IMMEDIATE, &cancel) == HK_ERR_CANCELLED);
    CHECK(hk_lights_set_level(&illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U, (hk_deadline_t){100U}, NULL) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(lights_normative_backend_effect_count() == 0U);
    flag = 0U;
    CHECK(hk_lights_set_level(&illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U, HK_DEADLINE_IMMEDIATE, &cancel) == HK_OK);
    CHECK(hk_lights_set_rgb(&rgb, 1000U, 500U, 1U, HK_DEADLINE_IMMEDIATE, NULL) == HK_OK);
    CHECK(hk_lights_set_level(&backlight, HK_LIGHTS_CHANNEL_BACKLIGHT, 900U, (hk_deadline_t){101U}, NULL) == HK_OK);
    CHECK(lights_normative_backend_active_mask() == HK_LIGHTS_CHANNEL_ALL);
    effects = lights_normative_backend_effect_count();
    CHECK(hk_lights_close(&backlight, (hk_deadline_t){100U}) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(backlight.service == service && lights_normative_backend_effect_count() == effects);
    CHECK(hk_lights_close(&backlight, HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(backlight.service == NULL && backlight.channels == 0U);
    CHECK(hk_lights_open(service, HK_LIGHTS_CHANNEL_BACKLIGHT, &replacement) == HK_OK);
    CHECK(hk_lights_close(&replacement, HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(hk_lights_retire(&illumination, HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(hk_lights_retire(&rgb, HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(lights_normative_backend_active_mask() == 0U);
    CHECK(lights_normative_backend_safe_off_mask() == HK_LIGHTS_CHANNEL_ALL);
    CHECK(hk_lights_open(service, HK_LIGHTS_CHANNEL_ILLUMINATION | HK_LIGHTS_CHANNEL_RGB, &replacement) == HK_OK);
    effects = lights_normative_backend_effect_count();
    CHECK(hk_lights_retire(&replacement, (hk_deadline_t){100U}) != HK_OK);
    CHECK(replacement.service == NULL && replacement.channels == 0U);
    CHECK(lights_normative_backend_effect_count() == effects);
    CHECK(hk_lights_open(service, HK_LIGHTS_CHANNEL_RGB, &rgb) == HK_ERR_INVALID_STATE);
    CHECK(hk_lights_open(service, HK_LIGHTS_CHANNEL_ILLUMINATION, &illumination) == HK_ERR_INVALID_STATE);
    CHECK(hk_lights_open(service, HK_LIGHTS_CHANNEL_BACKLIGHT, &backlight) == HK_OK);
    CHECK(hk_lights_close(&backlight, HK_DEADLINE_IMMEDIATE) == HK_OK);
    printf("LIGHTS_NORMATIVE_OK backend=%s conflict copy retry retire quarantine level_max=1000\n", lights_normative_backend_name());
    return 0;
}
