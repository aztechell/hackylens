#include <hackylens/capability/input.h>

#include <stdio.h>
#include <string.h>

#include "../firmware/src/capabilities/capability_provider.h"
#include "../firmware/src/capabilities/input_provider.h"
#include "../firmware/src/capabilities/input_state.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("INPUT_FAIL line=%d\n", __LINE__);                     \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static hk_capability_core_t s_core;
static hk_input_state_t s_state;
static hk_owner_t s_owner_a;
static hk_owner_t s_owner_b;

static hk_result_t fake_open(void *context, const hk_lease_t *lease)
{
    return hk_input_state_open_cursor((hk_input_state_t *)context, lease);
}

static hk_result_t fake_close(void *context, const hk_lease_t *lease)
{
    return hk_input_state_close_cursor((hk_input_state_t *)context, lease);
}

static hk_result_t fake_info(void *context, hk_input_info_t *info)
{
    (void)context;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_input_info_t){
        sizeof(*info), HK_INPUT_INFO_VERSION, HK_INPUT_BUTTON_ALL,
        HK_INPUT_SAMPLE_INTERVAL_US, HK_INPUT_DEBOUNCE_INTERVAL_US,
        HK_INPUT_EVENT_CAPACITY, 0U,
    };
    return HK_OK;
}

static hk_result_t fake_state(void *context, uint32_t *state)
{
    return hk_input_state_get((hk_input_state_t *)context, state);
}

static hk_result_t fake_event(
    void *context, const hk_lease_t *lease, hk_input_event_t *event)
{
    return hk_input_state_next_event(
        (hk_input_state_t *)context, lease, event);
}

static hk_input_provider_t s_input_provider = {
    .context = &s_state,
    .open_cursor = fake_open,
    .close_cursor = fake_close,
    .get_info = fake_info,
    .get_state = fake_state,
    .next_event = fake_event,
};
static const hk_capability_provider_t s_provider = {
    .context = &s_input_provider,
    .max_leases = 16U,
};
static const hk_capability_provider_t *s_provider_ref = &s_provider;
static const hk_capability_info_t s_inventory = {
    sizeof(hk_capability_info_t), HK_CAPABILITY_INFO_VERSION,
    HK_CAPABILITY_ID_INPUT, {0U, 1U, 0U, 0U}, HK_INPUT_FEATURES_0_1,
    HK_CAPABILITY_FLAG_SHARED, 0U, 0U, NULL, 0U, 0U,
};
static const hk_capability_grant_t s_grant = {
    .request = HK_INPUT_REQUEST_0_1_INIT,
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

static int accepted(uint64_t start_us, uint32_t raw)
{
    CHECK(hk_input_state_sample(&s_state, start_us, raw) == HK_OK);
    CHECK(hk_input_state_sample(
        &s_state, start_us + 10000U, raw) == HK_OK);
    CHECK(hk_input_state_sample(
        &s_state, start_us + 20000U, raw) == HK_OK);
    return 0;
}

int main(void)
{
    hk_input_t input_a;
    hk_input_t input_b;
    hk_input_event_t event;
    hk_input_info_t info;
    uint32_t state;
    uint64_t now = 0U;

    hk_input_state_reset(&s_state);
    CHECK(hk_input_state_sample(&s_state, now, 0U) == HK_OK);
    CHECK(hk_capability_core_init(
        &s_core, &s_inventory, &s_provider_ref, 1U) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &s_owner_a) == HK_OK);
    CHECK(hk_capability_core_owner_open(
        &s_core, &s_grant, 1U, &s_owner_b) == HK_OK);
    CHECK(hk_input_acquire(s_owner_a, &s_grant.request, &input_a) == HK_OK);
    CHECK(hk_input_acquire(s_owner_b, &s_grant.request, &input_b) == HK_OK);
    CHECK(hk_input_get_info(s_owner_a, &input_a, &info) == HK_OK);
    CHECK(info.supported_buttons == HK_INPUT_BUTTON_ALL &&
          info.sample_interval_us == 10000U &&
          info.debounce_interval_us == 20000U &&
          info.event_capacity == 8U);

    /* Bounce does not become a stable edge. */
    now = 10000U;
    CHECK(hk_input_state_sample(
        &s_state, now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    now += 10000U;
    CHECK(hk_input_state_sample(&s_state, now, 0U) == HK_OK);
    now += 10000U;
    CHECK(hk_input_state_sample(
        &s_state, now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    now += 10000U;
    CHECK(hk_input_state_sample(
        &s_state, now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    now += 10000U;
    CHECK(hk_input_state_sample(
        &s_state, now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_OK);
    CHECK(event.sequence == 1U && event.timestamp_us == now &&
          event.state == HK_INPUT_BUTTON_LEFT &&
          event.changed == HK_INPUT_BUTTON_LEFT &&
          event.pressed == HK_INPUT_BUTTON_LEFT && event.released == 0U);
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_PENDING);

    /* A held state never repeats, while the other lease sees the same edge. */
    now += 10000U;
    CHECK(hk_input_state_sample(
        &s_state, now, HK_INPUT_BUTTON_LEFT) == HK_OK);
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_PENDING);
    CHECK(hk_input_next_event(s_owner_b, &input_b, &event) == HK_OK);
    CHECK(event.sequence == 1U && event.pressed == HK_INPUT_BUTTON_LEFT);

    now += 10000U;
    CHECK(accepted(now, 0U) == 0);
    now += 20000U;
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_OK);
    CHECK(event.released == HK_INPUT_BUTTON_LEFT && event.pressed == 0U);

    now += 10000U;
    CHECK(accepted(
        now, HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_OK) == 0);
    now += 20000U;
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_OK);
    CHECK(event.changed == (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_OK) &&
          event.pressed == event.changed);
    CHECK(hk_input_get_state(s_owner_b, &input_b, &state) == HK_OK);
    CHECK(state == (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_OK));

    /* Leave owner B behind and overwrite its unread history. */
    for(uint32_t index = 0U; index < 9U; index++)
    {
        uint32_t raw = (index & 1U) ? HK_INPUT_BUTTON_BACK : 0U;
        now += 10000U;
        CHECK(accepted(now, raw) == 0);
        now += 20000U;
        CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_OK);
    }
    CHECK(hk_input_next_event(s_owner_b, &input_b, &event) == HK_ERR_OVERFLOW);
    CHECK(event.dropped == 11U && event.state == s_state.stable_state);
    CHECK(hk_input_next_event(s_owner_b, &input_b, &event) == HK_PENDING);

    CHECK(hk_capability_core_validate_lease(
        &s_core, s_owner_a, &input_a.lease, HK_CAPABILITY_ID_INPUT,
        1U, NULL) == HK_ERR_WRONG_CONTEXT);
    CHECK(hk_input_release(
        s_owner_a, (hk_deadline_t){UINT64_MAX}, &input_a) ==
        HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_input_next_event(s_owner_a, &input_a, &event) == HK_PENDING);
    CHECK(hk_input_release(
        s_owner_a, HK_DEADLINE_IMMEDIATE, &input_a) == HK_OK);
    CHECK(hk_input_get_state(s_owner_b, &input_b, &state) == HK_OK);
    CHECK(hk_input_release(
        s_owner_b, HK_DEADLINE_IMMEDIATE, &input_b) == HK_OK);

    printf("INPUT_CAPABILITY_OK events=%llu capacity=%u dropped=11\n",
           (unsigned long long)s_state.sequence,
           (unsigned)HK_INPUT_EVENT_CAPACITY);
    return 0;
}
