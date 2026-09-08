#ifndef HK_LIGHTS_PROVIDER_H
#define HK_LIGHTS_PROVIDER_H

#include <hackylens/capability/lights.h>

typedef struct
{
    const hk_lights_t *claimants[3];
    uint32_t quarantined;
    uint8_t prepared;
} hk_lights_state_t;

struct hk_lights_service
{
    hk_lights_state_t *state;
    void *context;
    hk_result_t (*prepare)(void *context);
    hk_result_t (*close_channels)(void *context, uint32_t channels,
        hk_deadline_t deadline);
    hk_result_t (*get_info)(void *context, hk_lights_info_t *info);
    hk_result_t (*set_level)(void *context, uint32_t channel,
        uint16_t level, hk_deadline_t deadline, const hk_cancel_t *cancel);
    hk_result_t (*set_rgb)(void *context, uint16_t red, uint16_t green,
        uint16_t blue, hk_deadline_t deadline, const hk_cancel_t *cancel);
};

extern const hk_lights_service_t hk_lights_binding;

#endif
