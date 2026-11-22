#include <stdio.h>
#include <string.h>

#include <hackylens/app.h>
#include <hackylens/app/camera.h>

static const hk_color_threshold_t s_threshold = {20U, 31U, 0U, 18U, 0U, 10U};

typedef enum
{
    TRACKING,
    OPEN_IMAGE,
    WRITE_IMAGE,
    OPEN_LOG,
    WRITE_LOG,
    PUBLISH_LOG,
} tracker_phase_t;

typedef struct
{
    tracker_phase_t phase;
    hk_app_camera_frame_t frame;
    hk_color_blob_t blob;
    uint32_t displayed;
    uint32_t stem;
    uint32_t saved;
    hk_result_t save_result;
    size_t log_length;
    size_t log_offset;
    char log[72];
    uint8_t save_requested;
} tracker_t;

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_storage[1024];
_Static_assert(sizeof(tracker_t) <= sizeof(s_storage), "color tracker state exceeds its slot");
static tracker_t *s_active;

static hk_result_t tracker_start(const hk_app_context_t *ctx)
{
    void *state;
    uint32_t capacity;
    hk_result_t result = hk_app_context_state(ctx, &state, &capacity);
    if(result != HK_OK)
        return result;
    if(capacity < sizeof(tracker_t))
        return HK_ERR_LIMIT;
    result = hk_app_camera_open(ctx);
    if(result == HK_OK)
    {
        s_active = state;
        *s_active = (tracker_t){0};
    }
    return result;
}

static void prepare_save(tracker_t *state, uint64_t now_us)
{
    const hk_color_blob_t *blob = &state->blob;
    state->save_requested = 0U;
    state->stem = (uint32_t)((now_us / 1000U) % 100000000U);
    if(blob->pixels)
        state->log_length = (size_t)snprintf(state->log, sizeof(state->log),
            "%lu (%u, %u, %u, %u, %lu)\n", (unsigned long)state->frame.sequence,
            blob->bounds.x, blob->bounds.y, blob->bounds.width, blob->bounds.height,
            (unsigned long)blob->pixels);
    else
        state->log_length = (size_t)snprintf(state->log, sizeof(state->log),
            "%lu None\n", (unsigned long)state->frame.sequence);
    state->log_offset = 0U;
    state->save_result = HK_PENDING;
    state->phase = OPEN_IMAGE;
}

static hk_result_t tracker_capture(const hk_app_context_t *ctx, tracker_t *state,
                                   uint64_t now_us)
{
    hk_result_t result = hk_app_camera_acquire(ctx, &state->frame);
    if(result != HK_OK)
        return result == HK_PENDING ? HK_OK : result;
    const uint32_t lease = state->frame.lease;
    result = hk_app_camera_find_color(ctx, lease, NULL, &s_threshold, 24U, &state->blob);
    if(result == HK_OK)
    {
        result = hk_app_camera_present(ctx, lease,
            state->blob.pixels ? &state->blob.bounds : NULL, UINT16_C(0x07e0));
        if(result == HK_OK)
            state->displayed++;
    }
    if(state->save_requested && (result == HK_OK || result == HK_ERR_BUSY))
    {
        prepare_save(state, now_us);
        return HK_OK;
    }
    hk_result_t release = hk_app_camera_release(ctx, lease);
    return result == HK_OK || result == HK_ERR_BUSY ? release : result;
}

static hk_result_t tracker_save(const hk_app_context_t *ctx, tracker_t *state)
{
    char name[sizeof("4294967295.BMP")];
    hk_result_t result;
    switch(state->phase)
    {
    case OPEN_IMAGE:
        snprintf(name, sizeof(name), "%lu.BMP", (unsigned long)state->stem);
        result = hk_app_camera_save_begin(ctx, state->frame.lease, name, strlen(name));
        break;
    case WRITE_IMAGE:
        result = hk_app_camera_save_step(ctx);
        if(result == HK_OK)
            result = hk_app_camera_release(ctx, state->frame.lease);
        break;
    case OPEN_LOG:
        snprintf(name, sizeof(name), "%lu.TXT", (unsigned long)state->stem);
        result = hk_app_data_begin(ctx, name, strlen(name));
        break;
    case WRITE_LOG:
    {
        size_t accepted = 0U;
        result = hk_app_data_write(ctx, (const uint8_t *)state->log + state->log_offset,
                                   state->log_length - state->log_offset, &accepted);
        state->log_offset += accepted;
        if(result >= 0 && state->log_offset != state->log_length)
            return HK_OK;
        break;
    }
    case PUBLISH_LOG:
        result = hk_app_data_finish(ctx);
        break;
    default:
        return HK_ERR_INVALID_STATE;
    }
    if(result == HK_PENDING)
        return HK_OK;
    if(result == HK_OK && state->phase != PUBLISH_LOG)
    {
        state->phase++;
        return HK_OK;
    }
    if(result == HK_OK)
        state->saved++;
    else if(state->phase <= WRITE_IMAGE)
    {
        (void)hk_app_camera_save_cancel(ctx);
        (void)hk_app_camera_release(ctx, state->frame.lease);
    }
    else
        (void)hk_app_data_cancel(ctx);
    state->save_result = result;
    state->phase = TRACKING;
    return HK_OK;
}

static hk_result_t tracker_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    tracker_t *state = s_active;
    if(event->kind == HK_APP_EVENT_INPUT)
    {
        if(event->data.input.pressed & HK_INPUT_BUTTON_BACK)
            return hk_app_context_request_close(ctx);
        if(event->data.input.pressed & HK_INPUT_BUTTON_OK)
            state->save_requested = 1U;
    }
    if(event->kind != HK_APP_EVENT_TIMER)
        return HK_OK;
    return state->phase == TRACKING ? tracker_capture(ctx, state, event->timestamp_us) :
        tracker_save(ctx, state);
}

static hk_result_t tracker_stop(const hk_app_context_t *ctx)
{
    s_active = NULL;
    /* The runtime also cancels an unfinished data file and retires the camera. */
    return hk_app_camera_close(ctx);
}

uint8_t color_tracker_debug(const char *command)
{
    tracker_t *state = s_active;
    if(strcmp(command, "HKCOLORSAVE") == 0)
    {
        if(state)
            state->save_requested = 1U;
        printf("HKCOLORSAVE active=%u\r\n", (unsigned)(state != NULL));
    }
    else if(strcmp(command, "HKCOLORINFO") == 0)
    {
        if(!state)
            printf("HKCOLORINFO active=0\r\n");
        else
            printf("HKCOLORINFO active=1 displayed=%lu seq=%lu width=%u height=%u "
                   "pixels=%lu phase=%u saved=%lu stem=%lu status=%ld\r\n",
                   (unsigned long)state->displayed, (unsigned long)state->frame.sequence,
                   state->frame.width, state->frame.height, (unsigned long)state->blob.pixels,
                   (unsigned)state->phase, (unsigned long)state->saved,
                   (unsigned long)state->stem, (long)state->save_result);
    }
    else
        return 0U;
    return 1U;
}

const hk_app_v2_entry_t color_tracker_v2_entry = {
    .state_storage = s_storage,
    .state_capacity_bytes = sizeof(s_storage),
    .start = tracker_start,
    .event = tracker_event,
    .stop = tracker_stop,
};
