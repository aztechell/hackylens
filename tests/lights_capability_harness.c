#include <hackylens/capability/lights.h>

#include <stdio.h>
#include <string.h>

#include "../firmware/src/capabilities/capability_provider.h"
#include "../firmware/src/capabilities/lights_provider.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("LIGHTS_FAIL line=%d\n", __LINE__);                     \
            return 1;                                                        \
        }                                                                    \
    } while(0)

typedef struct
{
    hk_lease_t lease;
    uint32_t channels;
    uint8_t active;
} fake_slot_t;

typedef struct
{
    fake_slot_t slots[8];
    uint64_t now_us;
    uint32_t write_count;
    uint32_t safe_off_mask;
    uint16_t backlight;
    uint16_t illumination;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
} fake_lights_t;

static hk_capability_core_t s_core;
static fake_lights_t s_fake;

static uint8_t owner_equal(hk_owner_t left, hk_owner_t right)
{
    return (uint8_t)(left.slot == right.slot &&
                     left.generation == right.generation);
}

static uint8_t lease_equal(const hk_lease_t *left, const hk_lease_t *right)
{
    return (uint8_t)(left && right && left->slot == right->slot &&
                     left->generation == right->generation &&
                     left->capability_id == right->capability_id &&
                     owner_equal(left->owner, right->owner));
}

static fake_slot_t *fake_find(fake_lights_t *fake, const hk_lease_t *lease)
{
    for(uint16_t index = 0U; index < 8U; index++)
    {
        if(fake->slots[index].active &&
           lease_equal(&fake->slots[index].lease, lease))
            return &fake->slots[index];
    }
    return NULL;
}

static void fake_safe_off(fake_lights_t *fake, uint32_t channels)
{
    fake->safe_off_mask |= channels;
    if(channels & HK_LIGHTS_CHANNEL_BACKLIGHT)
        fake->backlight = 0U;
    if(channels & HK_LIGHTS_CHANNEL_ILLUMINATION)
        fake->illumination = 0U;
    if(channels & HK_LIGHTS_CHANNEL_RGB)
        fake->red = fake->green = fake->blue = 0U;
}

static hk_result_t fake_open(
    void *context, const hk_lease_t *lease, uint32_t channels)
{
    fake_lights_t *fake = (fake_lights_t *)context;
    fake_slot_t *free_slot = NULL;

    for(uint16_t index = 0U; index < 8U; index++)
    {
        fake_slot_t *slot = &fake->slots[index];

        if(!slot->active)
        {
            if(!free_slot)
                free_slot = slot;
            continue;
        }
        if((slot->channels & channels) != 0U)
            return HK_ERR_BUSY;
    }
    if(!free_slot)
        return HK_ERR_LIMIT;
    free_slot->lease = *lease;
    free_slot->channels = channels;
    free_slot->active = 1U;
    return HK_OK;
}

static hk_result_t fake_close(void *context, const hk_lease_t *lease)
{
    fake_lights_t *fake = (fake_lights_t *)context;
    fake_slot_t *slot = fake_find(fake, lease);

    if(!slot)
        return HK_ERR_INTERNAL;
    fake_safe_off(fake, slot->channels);
    memset(slot, 0, sizeof(*slot));
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

static hk_result_t fake_validate_write(
    fake_lights_t *fake, const hk_lease_t *lease, uint32_t channels,
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    fake_slot_t *slot = fake_find(fake, lease);

    if(!slot)
        return HK_ERR_INTERNAL;
    if((slot->channels & channels) != channels)
        return HK_ERR_WRONG_OWNER;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(deadline.at_us != 0U && fake->now_us >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    return HK_OK;
}

static hk_result_t fake_level(
    void *context, const hk_lease_t *lease, uint32_t channel,
    uint16_t level, hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    fake_lights_t *fake = (fake_lights_t *)context;
    hk_result_t result = fake_validate_write(
        fake, lease, channel, deadline, cancel);

    if(result != HK_OK)
        return result;
    if(channel == HK_LIGHTS_CHANNEL_BACKLIGHT)
        fake->backlight = level;
    else if(channel == HK_LIGHTS_CHANNEL_ILLUMINATION)
        fake->illumination = level;
    else
        return HK_ERR_INVALID_ARGUMENT;
    fake->write_count++;
    return HK_OK;
}

static hk_result_t fake_rgb(
    void *context, const hk_lease_t *lease, uint16_t red,
    uint16_t green, uint16_t blue, hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    fake_lights_t *fake = (fake_lights_t *)context;
    hk_result_t result = fake_validate_write(
        fake, lease, HK_LIGHTS_CHANNEL_RGB, deadline, cancel);

    if(result != HK_OK)
        return result;
    fake->red = red;
    fake->green = green;
    fake->blue = blue;
    fake->write_count++;
    return HK_OK;
}

static hk_lights_provider_t s_lights_provider = {
    .context = &s_fake,
    .open_channels = fake_open,
    .close_channels = fake_close,
    .get_info = fake_info,
    .set_level = fake_level,
    .set_rgb = fake_rgb,
};

static hk_result_t fake_cleanup(
    void *context, hk_owner_t owner, hk_deadline_t deadline)
{
    hk_lights_provider_t *provider = (hk_lights_provider_t *)context;
    fake_lights_t *fake = (fake_lights_t *)provider->context;

    (void)deadline;
    for(uint16_t index = 0U; index < 8U; index++)
    {
        fake_slot_t *slot = &fake->slots[index];

        if(!slot->active || !owner_equal(slot->lease.owner, owner))
            continue;
        fake_safe_off(fake, slot->channels);
        memset(slot, 0, sizeof(*slot));
    }
    return HK_OK;
}

static hk_result_t fake_cleanup_lease(
    void *context, const hk_lease_t *lease, hk_deadline_t deadline)
{
    hk_lights_provider_t *provider = (hk_lights_provider_t *)context;
    fake_lights_t *fake = (fake_lights_t *)provider->context;

    (void)deadline;
    if(!fake_find(fake, lease))
        return HK_OK;
    return fake_close(fake, lease);
}

static hk_result_t fake_cleanup_dispatch(
    void *context, hk_owner_t owner, uint16_t target_core,
    hk_deadline_t deadline)
{
    return target_core == 0U ? fake_cleanup(context, owner, deadline) :
                              HK_ERR_WRONG_CONTEXT;
}

static const hk_capability_provider_t s_provider = {
    .context = &s_lights_provider,
    .cleanup_lease = fake_cleanup_lease,
    .cleanup = fake_cleanup,
    .cleanup_dispatch = fake_cleanup_dispatch,
    .max_leases = 8U,
};
static const hk_capability_provider_t *s_provider_ref = &s_provider;
static const hk_capability_info_t s_inventory = {
    sizeof(hk_capability_info_t), HK_CAPABILITY_INFO_VERSION,
    HK_CAPABILITY_ID_LIGHTS, {0U, 1U, 0U, 0U}, HK_LIGHTS_FEATURES_0_1,
    HK_CAPABILITY_FLAG_SHARED, 0U, 0U, NULL, 0U, 0U,
};
static const hk_capability_grant_t s_grant = {
    .request = HK_LIGHTS_REQUEST_0_1_INIT,
};

hk_result_t capability_owner_runtime_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    hk_capability_id_t expected_type, hk_lease_t *lease)
{
    return hk_capability_core_acquire(
        &s_core, owner, request, expected_type, 0U, lease);
}

hk_result_t capability_owner_runtime_release(
    hk_owner_t owner, hk_capability_id_t expected_type,
    hk_deadline_t deadline, hk_lease_t *lease)
{
    return hk_capability_core_release(
        &s_core, owner, expected_type, 0U, deadline, lease);
}

hk_result_t capability_owner_runtime_validate(
    hk_owner_t owner, const hk_lease_t *lease,
    hk_capability_id_t expected_type, void **provider_context)
{
    return hk_capability_core_validate_lease(
        &s_core, owner, lease, expected_type, 0U, provider_context);
}

hk_result_t capability_owner_runtime_quarantine(
    hk_owner_t owner, const hk_lease_t *lease,
    hk_capability_id_t expected_type)
{
    return hk_capability_core_quarantine_lease(
        &s_core, owner, lease, expected_type, 0U);
}

static uint8_t cancelled(const void *context)
{
    return *(const uint8_t *)context;
}

int main(void)
{
    hk_capability_request_t request = HK_LIGHTS_REQUEST_0_1_INIT;
    hk_owner_t owner_a;
    hk_owner_t owner_b;
    hk_owner_t owner_c;
    hk_owner_t owner_d;
    hk_owner_t owner_e;
    hk_lights_t illumination;
    hk_lights_t rgb;
    hk_lights_t backlight;
    hk_lights_t same_owner_backlight;
    hk_lights_t replacement;
    hk_lights_t stale_rgb;
    hk_lights_info_t info;
    uint8_t cancel_flag = 1U;
    hk_cancel_t cancel = {cancelled, &cancel_flag};
    hk_capability_grant_t illumination_grant = s_grant;
    hk_capability_request_t illumination_request = request;
    hk_capability_request_t short_request = request;

    memset(&s_fake, 0, sizeof(s_fake));
    s_fake.now_us = 100U;
    CHECK(hk_capability_core_init(
        &s_core, &s_inventory, &s_provider_ref, 1U) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &owner_a) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &owner_b) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &owner_c) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &owner_d) == HK_OK);
    illumination_grant.request.required_features =
        HK_LIGHTS_FEATURE_ILLUMINATION;
    illumination_request.required_features =
        HK_LIGHTS_FEATURE_ILLUMINATION;
    CHECK(hk_capability_core_owner_open(
        &s_core, &illumination_grant, 1U, &owner_e) == HK_OK);
    CHECK(hk_lights_acquire(
        owner_e, &illumination_request, HK_LIGHTS_CHANNEL_RGB,
        &replacement) == HK_ERR_NOT_DECLARED);

    CHECK(hk_lights_acquire(
        owner_a, &request, HK_LIGHTS_CHANNEL_ILLUMINATION,
        &illumination) == HK_OK);
    CHECK(hk_lights_acquire(
        owner_b, &request, HK_LIGHTS_CHANNEL_RGB, &rgb) == HK_OK);
    CHECK(hk_lights_acquire(
        owner_c, &request,
        HK_LIGHTS_CHANNEL_BACKLIGHT | HK_LIGHTS_CHANNEL_ILLUMINATION,
        &backlight) == HK_ERR_BUSY);
    CHECK(hk_lights_acquire(
        owner_c, &request, HK_LIGHTS_CHANNEL_BACKLIGHT,
        &backlight) == HK_OK);
    CHECK(hk_lights_get_info(owner_a, &illumination, &info) == HK_OK);
    CHECK(info.supported_channels == HK_LIGHTS_CHANNEL_ALL &&
          info.maximum_level == 1000U);

    CHECK(hk_lights_set_level(
        owner_b, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_WRONG_OWNER);
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_BACKLIGHT, 500U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_WRONG_OWNER);
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 1001U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U,
        HK_DEADLINE_IMMEDIATE, &cancel) == HK_ERR_CANCELLED);
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U,
        (hk_deadline_t){100U}, NULL) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(s_fake.write_count == 0U);

    cancel_flag = 0U;
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 500U,
        HK_DEADLINE_IMMEDIATE, &cancel) == HK_OK);
    CHECK(hk_lights_set_rgb(
        owner_b, &rgb, 1000U, 500U, 1U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_OK);
    CHECK(hk_lights_set_level(
        owner_c, &backlight, HK_LIGHTS_CHANNEL_BACKLIGHT, 900U,
        (hk_deadline_t){101U}, NULL) == HK_OK);
    CHECK(s_fake.write_count == 3U && s_fake.illumination == 500U &&
          s_fake.red == 1000U && s_fake.green == 500U &&
          s_fake.blue == 1U && s_fake.backlight == 900U);

    CHECK(hk_lights_release(
        owner_c, HK_DEADLINE_IMMEDIATE, &backlight) == HK_OK);
    CHECK(hk_lights_acquire(
        owner_a, &request, HK_LIGHTS_CHANNEL_BACKLIGHT,
        &same_owner_backlight) == HK_OK);
    CHECK(hk_lights_release(
        owner_a, HK_DEADLINE_IMMEDIATE, &same_owner_backlight) == HK_OK);
    CHECK(hk_lights_set_level(
        owner_a, &illumination, HK_LIGHTS_CHANNEL_ILLUMINATION, 600U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_OK);
    CHECK(hk_lights_release(
        owner_a, HK_DEADLINE_IMMEDIATE, &illumination) == HK_OK);
    CHECK(s_fake.illumination == 0U &&
          (s_fake.safe_off_mask & HK_LIGHTS_CHANNEL_ILLUMINATION));
    stale_rgb = rgb;
    CHECK(hk_capability_core_owner_close(
        &s_core, owner_b, 0U, HK_DEADLINE_IMMEDIATE) == HK_OK);
    CHECK(s_fake.red == 0U && s_fake.green == 0U && s_fake.blue == 0U);
    CHECK(hk_lights_set_rgb(
        owner_b, &stale_rgb, 1U, 1U, 1U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_ERR_STALE_HANDLE);
    CHECK(hk_lights_acquire(
        owner_d, &request, HK_LIGHTS_CHANNEL_RGB, &replacement) == HK_OK);
    CHECK(hk_lights_set_rgb(
        owner_d, &replacement, 25U, 50U, 75U,
        HK_DEADLINE_IMMEDIATE, NULL) == HK_OK);
    CHECK(hk_lights_release(
        owner_d, HK_DEADLINE_IMMEDIATE, &replacement) == HK_OK);
    CHECK(hk_lights_acquire(
        owner_d, &request, 0U, &replacement) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_lights_acquire(
        owner_d, &request, UINT32_C(0x80),
        &replacement) == HK_ERR_INVALID_ARGUMENT);
    short_request.struct_size = (uint16_t)(sizeof(short_request) - 1U);
    CHECK(hk_lights_acquire(
        owner_d, &short_request, HK_LIGHTS_CHANNEL_RGB,
        &replacement) == HK_ERR_INVALID_ARGUMENT);

    printf("LIGHTS_CAPABILITY_OK writes=%u safe_off_mask=0x%X level_max=%u\n",
           (unsigned)s_fake.write_count, (unsigned)s_fake.safe_off_mask,
           (unsigned)HK_LIGHTS_LEVEL_MAX);
    return 0;
}
