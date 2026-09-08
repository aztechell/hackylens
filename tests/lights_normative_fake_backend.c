#include "lights_normative_backend.h"

#include <hackylens/capability/lights.h>

#include <string.h>

#include "../firmware/src/capabilities/lights_provider.h"

typedef struct { uint64_t now_us; uint32_t effect_count, active_mask, safe_off_mask; } fake_lights_t;
static fake_lights_t s_fake;
static hk_lights_state_t s_state;
static void fake_safe_off(uint32_t channels)
{
    s_fake.safe_off_mask |= channels;
    s_fake.active_mask &= ~channels;
    s_fake.effect_count++;
}

static hk_result_t fake_close(void *context, uint32_t channels, hk_deadline_t deadline)
{
    (void)context;
    if(deadline.at_us && s_fake.now_us >= deadline.at_us) return HK_ERR_DEADLINE_EXCEEDED;
    fake_safe_off(channels);
    return HK_OK;
}
static hk_result_t fake_info(void *context, hk_lights_info_t *info)
{
    (void)context;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_lights_info_t){
        sizeof(*info), HK_LIGHTS_INFO_VERSION, HK_LIGHTS_CHANNEL_ALL,
        HK_LIGHTS_LEVEL_MAX, 0U,
    };
    return HK_OK;
}

static hk_result_t fake_validate_write(hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    if(cancel && cancel->probe && cancel->probe(cancel->context)) return HK_ERR_CANCELLED;
    if(deadline.at_us && s_fake.now_us >= deadline.at_us) return HK_ERR_DEADLINE_EXCEEDED;
    return HK_OK;
}
static hk_result_t fake_level(
    void *context, uint32_t channel,
    uint16_t level, hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    hk_result_t result;

    (void)context;
    result = fake_validate_write(deadline, cancel);
    if(result != HK_OK)
        return result;
    if(channel != HK_LIGHTS_CHANNEL_BACKLIGHT &&
       channel != HK_LIGHTS_CHANNEL_ILLUMINATION)
        return HK_ERR_INVALID_ARGUMENT;
    if(level != 0U)
        s_fake.active_mask |= channel;
    else
        s_fake.active_mask &= ~channel;
    s_fake.effect_count++;
    return HK_OK;
}

static hk_result_t fake_rgb(
    void *context, uint16_t red,
    uint16_t green, uint16_t blue, hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    hk_result_t result;

    (void)context;
    result = fake_validate_write(deadline, cancel);
    if(result != HK_OK)
        return result;
    if((red | green | blue) != 0U)
        s_fake.active_mask |= HK_LIGHTS_CHANNEL_RGB;
    else
        s_fake.active_mask &= ~HK_LIGHTS_CHANNEL_RGB;
    s_fake.effect_count++;
    return HK_OK;
}

static hk_result_t fake_prepare(void *context) { (void)context; return HK_OK; }

const hk_lights_service_t hk_lights_binding = {
    .prepare = fake_prepare,
    .state = &s_state, .context = &s_fake, .close_channels = fake_close,
    .get_info = fake_info, .set_level = fake_level, .set_rgb = fake_rgb,
};

const char *lights_normative_backend_name(void)
{
    return "fake";
}

void lights_normative_backend_reset(uint64_t now_us)
{
    memset(&s_fake, 0, sizeof(s_fake));
    memset(&s_state, 0, sizeof(s_state));
    s_fake.now_us = now_us;
}

void lights_normative_backend_set_now(uint64_t now_us)
{
    s_fake.now_us = now_us;
}

uint32_t lights_normative_backend_effect_count(void)
{
    return s_fake.effect_count;
}

uint32_t lights_normative_backend_active_mask(void)
{
    return s_fake.active_mask;
}

uint32_t lights_normative_backend_safe_off_mask(void)
{
    return s_fake.safe_off_mask;
}
