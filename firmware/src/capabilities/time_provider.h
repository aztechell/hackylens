#ifndef HK_TIME_PROVIDER_H
#define HK_TIME_PROVIDER_H

#include <hackylens/capability/time.h>

typedef hk_result_t (*hk_time_provider_now_fn)(
    void *context,
    uint64_t *value);
/* A successful nonzero sleep MUST make the next successful now_us observation
 * strictly greater than the observation immediately before that sleep. */
typedef hk_result_t (*hk_time_provider_sleep_fn)(
    void *context,
    uint64_t duration_us);

struct hk_time
{
    void *context;
    hk_time_provider_now_fn now_us;
    hk_time_provider_sleep_fn sleep_us;
    uint64_t max_sleep_us;
    uint32_t max_slice_us;
    /* Latch a provider-local fault; synchronized with now_us by the provider. */
    void (*fault)(void *context);
};

/* Selected platform binding; public access stays in portable time.c. */
extern const hk_time_t hk_time_binding;

#endif
