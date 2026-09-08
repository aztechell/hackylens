#ifndef HACKYLENS_CAPABILITY_LIGHTS_H
#define HACKYLENS_CAPABILITY_LIGHTS_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HK_CAPABILITY_ID_LIGHTS UINT32_C(0x00010005)

#define HK_LIGHTS_CHANNEL_BACKLIGHT (UINT32_C(1) << 0)
#define HK_LIGHTS_CHANNEL_ILLUMINATION (UINT32_C(1) << 1)
#define HK_LIGHTS_CHANNEL_RGB (UINT32_C(1) << 2)
#define HK_LIGHTS_CHANNEL_ALL                                      \
    (HK_LIGHTS_CHANNEL_BACKLIGHT | HK_LIGHTS_CHANNEL_ILLUMINATION | \
     HK_LIGHTS_CHANNEL_RGB)

#define HK_LIGHTS_FEATURE_BACKLIGHT (UINT64_C(1) << 0)
#define HK_LIGHTS_FEATURE_ILLUMINATION (UINT64_C(1) << 1)
#define HK_LIGHTS_FEATURE_RGB (UINT64_C(1) << 2)
#define HK_LIGHTS_FEATURES_0_1                                    \
    (HK_LIGHTS_FEATURE_BACKLIGHT | HK_LIGHTS_FEATURE_ILLUMINATION | \
     HK_LIGHTS_FEATURE_RGB)

#define HK_LIGHTS_LEVEL_MAX UINT16_C(1000)
#define HK_LIGHTS_INFO_VERSION 1U

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint32_t supported_channels;
    uint16_t maximum_level;
    uint16_t reserved;
} hk_lights_info_t;

/* Sessions must remain at their opening address until closed or retired. */
typedef struct hk_lights_service hk_lights_service_t;
typedef struct
{
    const hk_lights_service_t *service;
    uint32_t channels;
} hk_lights_t;

const hk_lights_service_t *hk_lights_service(void);
hk_result_t hk_lights_open(const hk_lights_service_t *service,
    uint32_t channels, hk_lights_t *session);
hk_result_t hk_lights_close(hk_lights_t *session, hk_deadline_t deadline);
hk_result_t hk_lights_retire(hk_lights_t *session, hk_deadline_t deadline);
hk_result_t hk_lights_get_info(const hk_lights_service_t *service,
    hk_lights_info_t *info);
hk_result_t hk_lights_set_level(const hk_lights_t *session,
    uint32_t channel, uint16_t level, hk_deadline_t deadline,
    const hk_cancel_t *cancel);
hk_result_t hk_lights_set_rgb(const hk_lights_t *session,
    uint16_t red, uint16_t green, uint16_t blue,
    hk_deadline_t deadline, const hk_cancel_t *cancel);

#ifdef __cplusplus
}
#endif

#endif
