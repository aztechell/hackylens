#ifndef HK_APP_RUNTIME_PRIVATE_H
#define HK_APP_RUNTIME_PRIVATE_H

#include <stddef.h>
#include <stdint.h>

#include <hackylens/capability/owner.h>
#include <hackylens/app/context.h>

#include "../core/hk_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HK_APP_RUNTIME_SLOT 1U
#define HK_APP_RUNTIME_TEARDOWN_BUDGET_US UINT64_C(100000)

typedef enum
{
    HK_APP_RUNTIME_INACTIVE = 0,
    HK_APP_RUNTIME_INJECTING,
    HK_APP_RUNTIME_PROBED,
    HK_APP_RUNTIME_PREPARED,
    HK_APP_RUNTIME_RUNNING,
    HK_APP_RUNTIME_STOPPING,
    HK_APP_RUNTIME_CLEANING,
    HK_APP_RUNTIME_FAULTED,
} hk_app_runtime_state_t;

typedef enum
{
    HK_APP_STAGE_REUSABLE = 0,
    HK_APP_STAGE_PROBING,
    HK_APP_STAGE_INJECTING,
    HK_APP_STAGE_PREPARING,
    HK_APP_STAGE_STARTING,
    HK_APP_STAGE_RUNNING,
    HK_APP_STAGE_STOPPING,
    HK_APP_STAGE_APP_CLEANUP,
    HK_APP_STAGE_OWNER_CLEANUP,
    HK_APP_STAGE_INVALIDATING,
} hk_app_runtime_stage_t;

typedef enum
{
    HK_APP_STOP_COMPLETED = 0,
    HK_APP_STOP_BACK = 1,
    HK_APP_STOP_SWITCH = 2,
    HK_APP_STOP_START_FAILED = 3,
    HK_APP_STOP_CALLBACK_FAILED = 4,
    HK_APP_STOP_DEADLINE = 5,
    HK_APP_STOP_FORCED = 6,
    HK_APP_STOP_SHUTDOWN = 7,
} hk_app_stop_reason_t;

typedef struct
{
    uint32_t kind;
    uint32_t value;
} hk_app_runtime_event_t;

typedef struct
{
    void *view;
    uint32_t flags;
} hk_app_runtime_surface_t;

typedef struct
{
    uint32_t slot;
    uint32_t context_generation;
    uint32_t epoch;
} hk_app_runtime_token_t;

typedef hk_result_t (*hk_app_probe_fn)(hk_app_context_t *ctx);
typedef hk_result_t (*hk_app_prepare_fn)(hk_app_context_t *ctx);
typedef hk_result_t (*hk_app_start_fn)(hk_app_context_t *ctx);
typedef hk_result_t (*hk_app_event_fn)(
    hk_app_context_t *ctx,
    const hk_app_runtime_event_t *event);
typedef hk_result_t (*hk_app_tick_fn)(hk_app_context_t *ctx, uint64_t now_us);
typedef hk_result_t (*hk_app_render_fn)(
    hk_app_context_t *ctx,
    hk_app_runtime_surface_t *surface);
typedef hk_result_t (*hk_app_stop_fn)(
    hk_app_context_t *ctx,
    hk_app_stop_reason_t reason);
typedef hk_result_t (*hk_app_cleanup_fn)(hk_app_context_t *ctx);

struct hk_app_v2_entry
{
    void *state_storage;
    uint32_t state_capacity_bytes;
    hk_app_probe_fn probe;
    hk_app_prepare_fn prepare;
    hk_app_start_fn start;
    hk_app_event_fn event;
    hk_app_tick_fn tick;
    hk_app_render_fn render;
    hk_app_stop_fn stop;
    hk_app_cleanup_fn cleanup;
};

typedef hk_result_t (*hk_app_runtime_resolve_capability_fn)(
    void *user,
    const hk_app_t *descriptor,
    const hk_app_capability_request_t *declaration,
    hk_capability_request_t *request);
typedef hk_result_t (*hk_app_runtime_resolve_service_fn)(
    void *user,
    const hk_app_t *descriptor,
    const hk_app_service_request_t *declaration);
typedef hk_result_t (*hk_app_runtime_owner_open_fn)(
    void *user,
    const hk_app_t *descriptor,
    hk_owner_t *owner);
typedef hk_result_t (*hk_app_runtime_acquire_capability_fn)(
    void *user,
    hk_owner_t owner,
    const hk_capability_request_t *request,
    hk_lease_t *lease);
typedef hk_result_t (*hk_app_runtime_acquire_service_fn)(
    void *user,
    hk_owner_t owner,
    const hk_app_service_request_t *declaration);
typedef hk_result_t (*hk_app_runtime_owner_cleanup_fn)(
    void *user,
    hk_owner_t owner,
    hk_deadline_t deadline);
typedef hk_result_t (*hk_app_runtime_deadline_after_fn)(
    void *user,
    uint64_t duration_us,
    hk_deadline_t *deadline);

typedef struct
{
    void *user;
    hk_app_runtime_resolve_capability_fn resolve_capability;
    hk_app_runtime_resolve_service_fn resolve_service;
    hk_app_runtime_owner_open_fn owner_open;
    hk_app_runtime_acquire_capability_fn acquire_capability;
    hk_app_runtime_acquire_service_fn acquire_service;
    hk_app_runtime_owner_cleanup_fn owner_cleanup;
    hk_app_runtime_deadline_after_fn deadline_after_us;
} hk_app_runtime_ops_t;

typedef struct hk_app_runtime
{
    hk_app_runtime_ops_t ops;
    const hk_app_t *descriptor;
    hk_app_context_t context;
    hk_capability_request_t
        resolved_capabilities[HK_APP_CONTEXT_MAX_CAPABILITIES];
    hk_deadline_t teardown_deadline;
    hk_app_runtime_state_t state;
    hk_app_runtime_stage_t stage;
    hk_app_stop_reason_t stop_reason;
    hk_result_t first_error;
    uint64_t teardown_budget_us;
    uint32_t context_generation;
    uint32_t active_epoch;
    uint8_t context_valid;
    uint8_t teardown_deadline_valid;
    uint8_t callback_active;
    uint8_t prepare_entered;
    uint8_t start_entered;
    uint8_t stop_called;
    uint8_t cleanup_called;
    uint8_t teardown_started;
    uint8_t retired;
} hk_app_runtime_t;

hk_result_t hk_app_runtime_init(
    hk_app_runtime_t *runtime,
    const hk_app_runtime_ops_t *ops,
    uint64_t teardown_budget_us);
hk_result_t hk_app_runtime_launch(
    hk_app_runtime_t *runtime,
    const hk_app_t *descriptor);
hk_result_t hk_app_runtime_stop(
    hk_app_runtime_t *runtime,
    hk_app_stop_reason_t reason);
hk_result_t hk_app_runtime_event(
    hk_app_runtime_t *runtime,
    const hk_app_runtime_event_t *event);
hk_result_t hk_app_runtime_tick(hk_app_runtime_t *runtime, uint64_t now_us);
hk_result_t hk_app_runtime_render(
    hk_app_runtime_t *runtime,
    hk_app_runtime_surface_t *surface);

hk_app_runtime_state_t hk_app_runtime_state(const hk_app_runtime_t *runtime);
hk_app_runtime_stage_t hk_app_runtime_stage(const hk_app_runtime_t *runtime);
hk_result_t hk_app_runtime_first_error(const hk_app_runtime_t *runtime);

hk_result_t hk_app_context_deferred_token(
    const hk_app_context_t *ctx,
    hk_app_runtime_token_t *token);
hk_result_t hk_app_runtime_validate_token(
    const hk_app_runtime_t *runtime,
    hk_app_runtime_token_t token);

#ifdef __cplusplus
}
#endif

#endif
