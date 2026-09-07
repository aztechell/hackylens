#include <hackylens/capability/time.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "time_normative_backend.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("TIME_NORMATIVE_FAIL backend=%s line=%d\n",            \
                   time_normative_backend_name(), __LINE__);                 \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static uint32_t s_cancel_polls;
static uint32_t s_cancel_on_poll;

static uint8_t cancel_probe(const void *context)
{
    (void)context;
    s_cancel_polls++;
    return (uint8_t)(s_cancel_on_poll != 0U &&
                     s_cancel_polls >= s_cancel_on_poll);
}

static int reset(const hk_time_t **time, uint64_t *base)
{
    s_cancel_polls = 0U;
    s_cancel_on_poll = 0U;
    *base = time_normative_backend_reset();
    *time = hk_time_service();
    CHECK(*time != NULL && *time == hk_time_service());
    return 0;
}

int main(int argc, char **argv)
{
    const hk_time_t *time;
    hk_deadline_t target;
    hk_deadline_t deadline;
    hk_cancel_t cancel = {cancel_probe, NULL};
    uint64_t base;
    uint64_t now;

    /* Faults are boot-lifetime state: each fault scenario gets a process. */
    if(argc == 2)
    {
        CHECK(reset(&time, &base) == 0);
        CHECK(hk_time_now_us(time, &now) == HK_OK && now == base);
        if(strcmp(argv[1], "freeze") == 0)
        {
            target.at_us = base + 1U;
            time_normative_backend_set_freeze(1U);
            CHECK(hk_time_sleep_until(time, target, target, NULL) == HK_ERR_INTERNAL);
            CHECK(time_normative_backend_sleep_calls() == 1U);
        }
        else
        {
            CHECK(strcmp(argv[1], "regress") == 0);
            time_normative_backend_set_now(base - 1U);
            CHECK(hk_time_now_us(time, &now) == HK_ERR_INTERNAL);
        }
        time_normative_backend_set_now(base + 100U);
        CHECK(hk_time_now_us(hk_time_service(), &now) == HK_ERR_INVALID_STATE);
        CHECK(hk_time_deadline_after_us(time, 1U, &deadline) == HK_ERR_INVALID_STATE);
        printf("TIME_FAULT_OK backend=%s scenario=%s\n",
               time_normative_backend_name(), argv[1]);
        return 0;
    }

    CHECK(reset(&time, &base) == 0);
    CHECK(hk_time_now_us(time, &now) == HK_OK && now == base);
    CHECK(hk_time_deadline_after_us(
        time, 12000U, &target) == HK_OK);
    CHECK(target.at_us == base + 12000U);
    CHECK(hk_time_sleep_until(
        time, target, target, NULL) == HK_OK);
    CHECK(time_normative_backend_sleep_calls() == 3U &&
          time_normative_backend_slept_us() == 12000U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base;
    deadline.at_us = 0U;
    s_cancel_on_poll = 1U;
    CHECK(hk_time_sleep_until(
        time, target, deadline, &cancel) == HK_OK);
    CHECK(time_normative_backend_sleep_calls() == 0U && s_cancel_polls == 0U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 1000U;
    deadline.at_us = 0U;
    CHECK(hk_time_sleep_until(
        time, target, deadline, NULL) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(time_normative_backend_sleep_calls() == 0U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 20000U;
    deadline.at_us = target.at_us;
    s_cancel_on_poll = 1U;
    CHECK(hk_time_sleep_until(
        time, target, deadline, &cancel) == HK_ERR_CANCELLED);
    CHECK(time_normative_backend_sleep_calls() == 0U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 20000U;
    deadline.at_us = target.at_us;
    s_cancel_on_poll = 3U;
    CHECK(hk_time_sleep_until(
        time, target, deadline, &cancel) == HK_ERR_CANCELLED);
    CHECK(time_normative_backend_sleep_calls() == 2U &&
          time_normative_backend_slept_us() == 10000U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 20000U;
    deadline.at_us = base + 10000U;
    s_cancel_on_poll = 3U;
    CHECK(hk_time_sleep_until(
        time, target, deadline, &cancel) == HK_ERR_CANCELLED);
    CHECK(time_normative_backend_slept_us() == 10000U);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 10000U;
    deadline.at_us = target.at_us;
    s_cancel_on_poll = 3U;
    CHECK(hk_time_sleep_until(
        time, target, deadline, &cancel) == HK_OK);

    CHECK(reset(&time, &base) == 0);
    target.at_us = base + 10001U;
    deadline.at_us = base + 6501U;
    CHECK(hk_time_sleep_until(time, target, deadline, NULL) == HK_ERR_DEADLINE_EXCEEDED);
    CHECK(time_normative_backend_sleep_calls() == 2U);
    CHECK(time_normative_backend_slept_us() == 6501U);
    CHECK(hk_time_now_us(time, &now) == HK_OK && now == deadline.at_us);

    CHECK(reset(&time, &base) == 0);
    CHECK(hk_time_now_us(NULL, &now) == HK_ERR_CAPABILITY_ABSENT);
    CHECK(hk_time_now_us(time, NULL) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_time_deadline_after_us(time, 0U, NULL) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_time_deadline_after_us(time, 0U, &deadline) == HK_OK);
    CHECK(deadline.at_us == base);
    target.at_us = UINT64_MAX;
    CHECK(hk_time_sleep_until(time, target, deadline, NULL) == HK_ERR_INVALID_ARGUMENT);
    target.at_us = base + 1U;
    deadline.at_us = UINT64_MAX;
    CHECK(hk_time_sleep_until(time, target, deadline, NULL) == HK_ERR_INVALID_ARGUMENT);
    target.at_us = base + HK_TIME_MAX_SLEEP_US + 1U;
    deadline.at_us = target.at_us;
    CHECK(hk_time_sleep_until(time, target, deadline, NULL) == HK_ERR_LIMIT);
    CHECK(time_normative_backend_sleep_calls() == 0U);

    /* Keep the near-UINT64_MAX clock case last: the real K210 provider's
       process-lifetime monotonic guard intentionally cannot be reset. */
    CHECK(reset(&time, &base) == 0);
    CHECK(hk_time_deadline_after_us(
        time, HK_TIME_MAX_SLEEP_US + 1U,
        &deadline) == HK_ERR_LIMIT);
    time_normative_backend_set_now(UINT64_MAX - 4U);
    CHECK(hk_time_deadline_after_us(
        time, 4U, &deadline) == HK_ERR_LIMIT);

    printf("TIME_NORMATIVE_OK backend=%s cases=10 max_slice_us=%llu\n",
           time_normative_backend_name(),
           (unsigned long long)HK_TIME_CANCEL_PROBE_MAX_US);
    return 0;
}
