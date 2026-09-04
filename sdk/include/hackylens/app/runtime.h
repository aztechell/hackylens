#ifndef HACKYLENS_APP_RUNTIME_H
#define HACKYLENS_APP_RUNTIME_H

#include <stdint.h>

#include <hackylens/app/context.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HK_APP_EVENT_VERSION 1U
#define HK_APP_WAKEUP_TOKEN_VERSION 1U
#define HK_APP_SURFACE_VERSION 1U
#define HK_APP_MAX_INVALIDATIONS 8U
#define HK_APP_STATE_ALIGNMENT 16U

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

typedef enum
{
    HK_APP_EVENT_INPUT = 1,
    HK_APP_EVENT_MEDIA = 2,
    HK_APP_EVENT_TIMER = 3,
    HK_APP_EVENT_RUNTIME_CLOSE = 4,
    HK_APP_EVENT_WAKEUP = 5,
} hk_app_event_kind_t;

typedef enum
{
    HK_APP_MEDIA_INSERTED = 1,
    HK_APP_MEDIA_REMOVED = 2,
    HK_APP_MEDIA_MOUNTED = 3,
    HK_APP_MEDIA_ERROR = 4,
} hk_app_media_kind_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    uint32_t slot;
    uint32_t context_generation;
    uint32_t epoch;
    uint32_t value;
} hk_app_wakeup_token_t;

typedef struct
{
    uint16_t struct_size;
    uint16_t struct_version;
    hk_app_event_kind_t kind;
    uint32_t reserved;
    uint64_t sequence;
    uint64_t timestamp_us;
    union
    {
        hk_input_event_t input;
        struct
        {
            hk_app_media_kind_t kind;
            uint32_t generation;
        } media;
        struct
        {
            uint64_t scheduled_us;
            uint64_t now_us;
        } timer;
        struct
        {
            hk_app_stop_reason_t reason;
            uint32_t reserved;
        } close;
        struct
        {
            hk_app_wakeup_token_t token;
        } wakeup;
    } data;
} hk_app_event_t;

typedef struct hk_app_surface hk_app_surface_t;

typedef hk_result_t (*hk_app_start_fn)(const hk_app_context_t *ctx);
typedef hk_result_t (*hk_app_event_fn)(
    const hk_app_context_t *ctx,
    const hk_app_event_t *event);
typedef hk_result_t (*hk_app_render_fn)(
    const hk_app_context_t *ctx,
    hk_app_surface_t *surface);
typedef hk_result_t (*hk_app_stop_fn)(const hk_app_context_t *ctx);

typedef struct hk_app_v2_entry
{
    void *state_storage;
    uint32_t state_capacity_bytes;
    hk_app_start_fn start;
    hk_app_event_fn event;
    hk_app_render_fn render;
    hk_app_stop_fn stop;
} hk_app_v2_entry_t;

hk_result_t hk_app_context_request_render(
    const hk_app_context_t *ctx,
    const hk_display_rect_t *region);
hk_result_t hk_app_context_request_close(const hk_app_context_t *ctx);
hk_result_t hk_app_context_wakeup_token(
    const hk_app_context_t *ctx,
    uint32_t value,
    hk_app_wakeup_token_t *token);

hk_result_t hk_app_surface_get_info(
    const hk_app_surface_t *surface,
    hk_display_info_t *info);
hk_result_t hk_app_surface_invalidate(
    hk_app_surface_t *surface,
    const hk_display_rect_t *region);
hk_result_t hk_app_surface_clear(
    hk_app_surface_t *surface,
    uint16_t rgb565);
hk_result_t hk_app_surface_fill_rect(
    hk_app_surface_t *surface,
    const hk_display_rect_t *rect,
    uint16_t rgb565);
hk_result_t hk_app_surface_stroke_rect(
    hk_app_surface_t *surface,
    const hk_display_rect_t *rect,
    uint16_t rgb565);
hk_result_t hk_app_surface_text(
    hk_app_surface_t *surface,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565);
hk_result_t hk_app_surface_blit(
    hk_app_surface_t *surface,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format);

#ifdef __cplusplus
}
#endif

#endif
