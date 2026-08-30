#ifndef HACKYLENS_APP_CONTEXT_H
#define HACKYLENS_APP_CONTEXT_H

#include <stdint.h>

#include <hackylens/capability/display.h>
#include <hackylens/capability/external_link.h>
#include <hackylens/capability/input.h>
#include <hackylens/capability/lights.h>
#include <hackylens/capability/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HK_APP_CONTEXT_VERSION 1U
#define HK_APP_CONTEXT_MAX_CAPABILITIES 16U
#define HK_APP_CONTEXT_MAX_SERVICES 16U

typedef struct
{
    hk_capability_id_t id;
    hk_lease_t lease;
    const char *fallback;
    uint16_t instance;
    uint8_t optional;
    uint8_t available;
} hk_app_capability_grant_t;

typedef struct
{
    const char *id;
    const char *namespace_name;
    hk_owner_t owner;
    uint32_t context_generation;
} hk_app_service_t;

typedef struct hk_app_context
{
    uint16_t struct_size;
    uint16_t struct_version;
    const char *app_id;
    hk_owner_t owner;
    uint32_t generation;
    hk_app_capability_grant_t capabilities[HK_APP_CONTEXT_MAX_CAPABILITIES];
    hk_app_service_t services[HK_APP_CONTEXT_MAX_SERVICES];
    uint16_t capability_count;
    uint16_t service_count;
} hk_app_context_t;

hk_result_t hk_app_context_identity(
    const hk_app_context_t *ctx,
    const char **app_id,
    uint32_t *generation,
    hk_owner_t *owner);
hk_result_t hk_app_context_capability_status(
    const hk_app_context_t *ctx,
    hk_capability_id_t id,
    uint16_t instance,
    uint8_t *available,
    const char **fallback);
hk_result_t hk_app_context_time(
    const hk_app_context_t *ctx,
    uint16_t instance,
    hk_time_t *handle);
hk_result_t hk_app_context_input(
    const hk_app_context_t *ctx,
    uint16_t instance,
    hk_input_t *handle);
hk_result_t hk_app_context_display(
    const hk_app_context_t *ctx,
    uint16_t instance,
    hk_display_t *handle);
hk_result_t hk_app_context_external_link(
    const hk_app_context_t *ctx,
    uint16_t instance,
    hk_external_link_t *handle);
hk_result_t hk_app_context_lights(
    const hk_app_context_t *ctx,
    uint16_t instance,
    hk_lights_t *handle);
hk_result_t hk_app_context_service(
    const hk_app_context_t *ctx,
    const char *id,
    hk_app_service_t *handle);
hk_result_t hk_app_context_state(
    const hk_app_context_t *ctx,
    void **state,
    uint32_t *size_bytes);
hk_result_t hk_app_context_teardown_deadline(
    const hk_app_context_t *ctx,
    hk_deadline_t *deadline);

#ifdef __cplusplus
}
#endif

#endif
