#include <hackylens/capability/time.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/src/capabilities/capability_provider.h"
#include "../firmware/src/capabilities/time_provider.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("TIME_FAIL line=%d\n", __LINE__);                       \
            return 1;                                                        \
        }                                                                    \
    } while(0)

typedef struct
{
    uint64_t now_us;
    uint64_t last_us;
    uint64_t slept_us;
    uint32_t sleep_calls;
    uint32_t cancel_polls;
    uint32_t cancel_on_poll;
    uint8_t observed;
    uint8_t freeze;
} fake_time_t;

static hk_capability_core_t s_core;
static hk_owner_t s_owner;
static fake_time_t s_fake;
static hk_time_provider_t s_time_provider;
static hk_capability_provider_t s_provider;
static const hk_capability_provider_t *s_provider_ref = &s_provider;
static const hk_capability_limit_t s_limits[] = {
    {sizeof(hk_capability_limit_t), HK_CAPABILITY_LIMIT_VERSION,
     1U, HK_TIME_MAX_SLEEP_US},
};
static const hk_capability_info_t s_inventory = {
    sizeof(hk_capability_info_t), HK_CAPABILITY_INFO_VERSION,
    HK_CAPABILITY_ID_TIME, {0U, 1U, 0U, 0U}, HK_TIME_FEATURES_0_1,
    HK_CAPABILITY_FLAG_SHARED, 0U, HK_CAPABILITY_CORE_ANY,
    s_limits, 1U, 0U,
};
static const hk_capability_grant_t s_grant = {
    .request = HK_TIME_REQUEST_0_1_INIT,
};

static hk_result_t fake_now(void *context, uint64_t *value)
{
    fake_time_t *fake = (fake_time_t *)context;

    if(fake->observed && fake->now_us < fake->last_us)
        return HK_ERR_INTERNAL;
    fake->last_us = fake->now_us;
    fake->observed = 1U;
    *value = fake->now_us;
    return HK_OK;
}

static hk_result_t fake_sleep(void *context, uint64_t duration_us)
{
    fake_time_t *fake = (fake_time_t *)context;

    CHECK(duration_us > 0U && duration_us <= HK_TIME_CANCEL_PROBE_MAX_US);
    fake->sleep_calls++;
    fake->slept_us += duration_us;
    if(!fake->freeze)
        fake->now_us += duration_us;
    return HK_OK;
}

static uint8_t fake_cancel(const void *context)
{
    fake_time_t *fake = (fake_time_t *)context;

    fake->cancel_polls++;
    return (uint8_t)(fake->cancel_on_poll != 0U &&
                     fake->cancel_polls >= fake->cancel_on_poll);
}

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

static int reset(hk_time_t *time)
{
    memset(&s_core, 0, sizeof(s_core));
    memset(&s_fake, 0, sizeof(s_fake));
    s_fake.now_us = 1000000U;
    s_time_provider.context = &s_fake;
    s_time_provider.now_us = fake_now;
    s_time_provider.sleep_us = fake_sleep;
    s_time_provider.max_sleep_us = HK_TIME_MAX_SLEEP_US;
    s_time_provider.max_slice_us = (uint32_t)HK_TIME_CANCEL_PROBE_MAX_US;
    s_time_provider.reserved = 0U;
    memset(&s_provider, 0, sizeof(s_provider));
    s_provider.context = &s_time_provider;
    s_provider.max_leases = 16U;
    CHECK(hk_capability_core_init(
        &s_core, &s_inventory, &s_provider_ref, 1U) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &s_owner) == HK_OK);
    CHECK(hk_time_acquire(s_owner, &s_grant.request, time) == HK_OK);
    return 0;
}

int main(void)
{
    hk_time_t time;
    hk_deadline_t target;
    hk_deadline_t deadline;
    hk_cancel_t cancel = {fake_cancel, &s_fake};
    uint64_t now;

    CHECK(reset(&time) == 0);
    CHECK(hk_time_now_us(s_owner, &time, &now) == HK_OK);
    CHECK(now == 1000000U);
    CHECK(hk_time_deadline_after_us(
        s_owner, &time, 12000U, &target) == HK_OK);
    CHECK(target.at_us == 1012000U);
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, target, NULL) == HK_OK);
    CHECK(s_fake.sleep_calls == 3U && s_fake.slept_us == 12000U);

    CHECK(reset(&time) == 0);
    target.at_us = s_fake.now_us;
    deadline.at_us = 0U;
    s_fake.cancel_on_poll = 1U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, &cancel) == HK_OK);
    CHECK(s_fake.sleep_calls == 0U && s_fake.cancel_polls == 0U);

    CHECK(reset(&time) == 0);
    target.at_us = s_fake.now_us + 1000U;
    deadline.at_us = 0U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, NULL) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(s_fake.sleep_calls == 0U);

    CHECK(reset(&time) == 0);
    target.at_us = s_fake.now_us + 20000U;
    deadline.at_us = target.at_us;
    s_fake.cancel_on_poll = 1U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, &cancel) == HK_ERR_CANCELLED);
    CHECK(s_fake.sleep_calls == 0U);

    CHECK(reset(&time) == 0);
    target.at_us = s_fake.now_us + 20000U;
    deadline.at_us = target.at_us;
    s_fake.cancel_on_poll = 3U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, &cancel) == HK_ERR_CANCELLED);
    CHECK(s_fake.sleep_calls == 2U && s_fake.slept_us == 10000U);

    CHECK(reset(&time) == 0);
    target.at_us = s_fake.now_us + 20000U;
    deadline.at_us = s_fake.now_us + 10000U;
    s_fake.cancel_on_poll = 3U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, &cancel) == HK_ERR_CANCELLED);
    CHECK(s_fake.slept_us == 10000U);

    CHECK(reset(&time) == 0);
    target.at_us = s_fake.now_us + 10000U;
    deadline.at_us = target.at_us;
    s_fake.cancel_on_poll = 3U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, &cancel) == HK_OK);

    CHECK(reset(&time) == 0);
    CHECK(hk_time_deadline_after_us(
        s_owner, &time, HK_TIME_MAX_SLEEP_US + 1U,
        &deadline) == HK_ERR_LIMIT);
    s_fake.now_us = UINT64_MAX - 4U;
    s_fake.observed = 0U;
    CHECK(hk_time_deadline_after_us(
        s_owner, &time, 4U, &deadline) == HK_ERR_LIMIT);

    CHECK(reset(&time) == 0);
    target.at_us = s_fake.now_us + 1U;
    deadline.at_us = target.at_us;
    s_fake.freeze = 1U;
    CHECK(hk_time_sleep_until(
        s_owner, &time, target, deadline, NULL) == HK_ERR_INTERNAL);
    CHECK(hk_time_now_us(s_owner, &time, &now) == HK_ERR_INVALID_STATE);

    CHECK(reset(&time) == 0);
    CHECK(hk_time_now_us(s_owner, &time, &now) == HK_OK);
    s_fake.now_us--;
    CHECK(hk_time_now_us(s_owner, &time, &now) == HK_ERR_INTERNAL);
    CHECK(hk_time_release(
        s_owner, HK_DEADLINE_IMMEDIATE, &time) == HK_OK);

    printf("TIME_CAPABILITY_OK cases=10 max_slice_us=%llu\n",
           (unsigned long long)HK_TIME_CANCEL_PROBE_MAX_US);
    return 0;
}
