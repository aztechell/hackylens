#include "../../../firmware/src/capabilities/lights_provider.h"
#include "../../../firmware/src/drivers/hk_lights.h"

#include <hackylens/capability/lights.h>

#include <stddef.h>

#include "../hal/hal_time.h"

static hk_lights_state_t s_lights;

static void safe_off(uint32_t channels)
{
    if(channels & HK_LIGHTS_CHANNEL_BACKLIGHT)
        lights_screen_backlight_off();
    if(channels & HK_LIGHTS_CHANNEL_ILLUMINATION)
        lights_illum_set(0U, 0U);
    if(channels & HK_LIGHTS_CHANNEL_RGB)
        lights_rgb_set(0U, 0U, 0U, 0U);
}

static uint8_t deadline_expired(hk_deadline_t deadline)
{
    return (uint8_t)(deadline.at_us != 0U &&
                     hal_time_us() >= deadline.at_us);
}

static hk_result_t k210_lights_prepare(void *context)
{
    (void)context;
    lights_driver_prepare();
    return HK_OK;
}

static hk_result_t k210_lights_close(
    void *context, uint32_t channels, hk_deadline_t deadline)
{
    (void)context;
    if(deadline_expired(deadline))
        return HK_ERR_DEADLINE_EXCEEDED;
    safe_off(channels);
    return HK_OK;
}

static hk_result_t k210_lights_info(
    void *context, hk_lights_info_t *info)
{
    (void)context;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_lights_info_t){
        sizeof(hk_lights_info_t), HK_LIGHTS_INFO_VERSION,
        HK_LIGHTS_CHANNEL_ALL, HK_LIGHTS_LEVEL_MAX, 0U,
    };
    return HK_OK;
}

static hk_result_t validate_write(
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(deadline_expired(deadline))
        return HK_ERR_DEADLINE_EXCEEDED;
    return HK_OK;
}

static uint8_t percent(uint16_t level)
{
    return (uint8_t)((level + 5U) / 10U);
}

static hk_result_t k210_lights_set_level(
    void *context, uint32_t channel,
    uint16_t level, hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    (void)context;
    hk_result_t result = validate_write(deadline, cancel);

    if(result != HK_OK)
        return result;
    if(channel == HK_LIGHTS_CHANNEL_BACKLIGHT)
    {
        if(level == 0U)
            lights_screen_backlight_off();
        else
            lights_screen_backlight_set(percent(level));
        return HK_OK;
    }
    if(channel == HK_LIGHTS_CHANNEL_ILLUMINATION)
    {
        lights_illum_set(level != 0U, percent(level));
        return HK_OK;
    }
    return HK_ERR_INVALID_ARGUMENT;
}

static hk_result_t k210_lights_set_rgb(
    void *context, uint16_t red,
    uint16_t green, uint16_t blue, hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    (void)context;
    hk_result_t result = validate_write(deadline, cancel);

    if(result != HK_OK)
        return result;
    lights_rgb_set((red | green | blue) != 0U,
                   percent(red), percent(green), percent(blue));
    return HK_OK;
}

const hk_lights_service_t hk_lights_binding = {
    .state = &s_lights,
    .prepare = k210_lights_prepare,
    .close_channels = k210_lights_close,
    .get_info = k210_lights_info,
    .set_level = k210_lights_set_level,
    .set_rgb = k210_lights_set_rgb,
};
