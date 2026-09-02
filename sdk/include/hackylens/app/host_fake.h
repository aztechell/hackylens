#ifndef HACKYLENS_APP_HOST_FAKE_H
#define HACKYLENS_APP_HOST_FAKE_H

#include <hackylens/app.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HK_APP_HOST_FAKE_VERSION 1U
#define HK_APP_HOST_FAKE_STORAGE_BYTES 4096U
#define HK_APP_HOST_FAKE_INPUT_CAPACITY 8U

typedef enum
{
    HK_APP_HOST_FAKE_INACTIVE = 0,
    HK_APP_HOST_FAKE_PROBING,
    HK_APP_HOST_FAKE_PREPARING,
    HK_APP_HOST_FAKE_STARTING,
    HK_APP_HOST_FAKE_RUNNING,
    HK_APP_HOST_FAKE_STOPPING,
    HK_APP_HOST_FAKE_CLEANING,
} hk_app_host_fake_state_t;

typedef enum
{
    HK_APP_HOST_FAKE_FAIL_NONE = 0,
    HK_APP_HOST_FAKE_FAIL_PROBE,
    HK_APP_HOST_FAKE_FAIL_GRANT_TIME,
    HK_APP_HOST_FAKE_FAIL_GRANT_INPUT,
    HK_APP_HOST_FAKE_FAIL_GRANT_DISPLAY,
    HK_APP_HOST_FAKE_FAIL_GRANT_SERVICE,
    HK_APP_HOST_FAKE_FAIL_PREPARE,
    HK_APP_HOST_FAKE_FAIL_START,
    HK_APP_HOST_FAKE_FAIL_EVENT,
    HK_APP_HOST_FAKE_FAIL_TICK,
    HK_APP_HOST_FAKE_FAIL_RENDER,
    HK_APP_HOST_FAKE_FAIL_STOP,
    HK_APP_HOST_FAKE_FAIL_CLEANUP,
    HK_APP_HOST_FAKE_FAIL_OWNER_CLEANUP,
} hk_app_host_fake_failure_point_t;

typedef struct
{
    hk_capability_id_t id;
    uint16_t instance;
    uint8_t optional;
    uint8_t available;
    const char *fallback;
} hk_app_host_fake_grant_t;

typedef struct
{
    const char *id;
    const char *namespace_name;
} hk_app_host_fake_service_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    const char *app_id;
    const hk_app_v2_entry_t *entry;
    const hk_app_host_fake_grant_t *grants;
    uint16_t grant_count;
    const hk_app_host_fake_service_t *services;
    uint16_t service_count;
    uint16_t reserved;
    uint32_t state_bytes;
    uint32_t tick_interval_us;
    uint32_t tick_budget_us;
    uint32_t render_budget_us;
    uint64_t initial_time_us;
    uint64_t teardown_budget_us;
    uint32_t display_width;
    uint32_t display_height;
    hk_buffer_view_t display_surface;
} hk_app_host_fake_config_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    hk_app_host_fake_state_t state;
    hk_result_t first_error;
    uint64_t now_us;
    uint64_t event_sequence;
    uint32_t context_generation;
    uint32_t probe_calls;
    uint32_t prepare_calls;
    uint32_t start_calls;
    uint32_t event_calls;
    uint32_t tick_calls;
    uint32_t render_calls;
    uint32_t stop_calls;
    uint32_t cleanup_calls;
    uint32_t owner_cleanup_calls;
    uint32_t display_operations;
    uint32_t display_present_calls;
    uint8_t render_pending;
    uint8_t reserved[3];
} hk_app_host_fake_snapshot_t;

typedef union
{
    uint64_t alignment;
    uint8_t bytes[HK_APP_HOST_FAKE_STORAGE_BYTES];
} hk_app_host_fake_t;

hk_result_t hk_app_host_fake_initialize(
    hk_app_host_fake_t *fake,
    const hk_app_host_fake_config_t *config);
hk_result_t hk_app_host_fake_set_failure(
    hk_app_host_fake_t *fake,
    hk_app_host_fake_failure_point_t point,
    hk_result_t result);
hk_result_t hk_app_host_fake_advance_time(
    hk_app_host_fake_t *fake,
    uint64_t delta_us);
hk_result_t hk_app_host_fake_launch(hk_app_host_fake_t *fake);
hk_result_t hk_app_host_fake_input(
    hk_app_host_fake_t *fake,
    uint32_t state);
hk_result_t hk_app_host_fake_media(
    hk_app_host_fake_t *fake,
    hk_app_media_kind_t kind,
    uint32_t generation);
hk_result_t hk_app_host_fake_event(
    hk_app_host_fake_t *fake,
    const hk_app_event_t *event);
hk_result_t hk_app_host_fake_tick(hk_app_host_fake_t *fake);
hk_result_t hk_app_host_fake_render(hk_app_host_fake_t *fake);
hk_result_t hk_app_host_fake_stop(
    hk_app_host_fake_t *fake,
    hk_app_stop_reason_t reason);
hk_result_t hk_app_host_fake_snapshot(
    const hk_app_host_fake_t *fake,
    hk_app_host_fake_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
