#include "camera_code_client.h"

#include <stddef.h>

#include "hk_config.h"
#if HK_ENABLE_QR_DECODER || HK_ENABLE_APRILTAG_DECODER
#include <hackylens/capability/time.h>
#include "camera_client.h"
#include "camera_code_decoder.h"
#include "core1_executor.h"
#include "resource_cleanup.h"
#include "../core/shared_buffer.h"

#define CODE_SCAN_TIMEOUT_US 2000000ULL

/* Exclusive executor jobs use the uncached alias. This exchange never holds
 * app memory or a DVP pointer, so a stopped app can release its frame. */
typedef struct __attribute__((aligned(64)))
{
    vision_result_snapshot_t snapshot;
    vision_source_t source;
    camera_code_frame_t frame;
    uint8_t *image;
    hk_result_t prepare_result;
} code_exchange_t;

static code_exchange_t s_exchange;

typedef enum
{
    CODE_IDLE,
    CODE_PREPARING,
    CODE_STAGING,
    CODE_DECODING,
    CODE_RETIRING,
    CODE_CLOSING,
} code_phase_t;

static struct
{
    const void *owner;
    code_phase_t phase;
    uint32_t lease;
    uint32_t ticket;
    hk_deadline_t deadline;
    uint8_t pass;
} s_client;

static void prepare_job(void *context)
{
    code_exchange_t *exchange = context;
    exchange->prepare_result = camera_code_prepare(exchange->source, &exchange->image);
}

static void decode_job(void *context)
{
    code_exchange_t *exchange = context;
    camera_code_decode(exchange->source, &exchange->frame, exchange->image, &exchange->snapshot);
}

static void close_job(void *context)
{
    (void)context;
    camera_code_close();
}

static uint8_t finish_close(void)
{
    if(s_client.ticket && !core1_executor_complete(s_client.ticket))
        return 0U;
    if(s_client.phase == CODE_RETIRING)
    {
        s_client.ticket = core1_executor_submit(close_job, NULL);
        if(s_client.ticket)
            s_client.phase = CODE_CLOSING;
        return 0U;
    }
    s_client.ticket = 0U;
    s_client.phase = CODE_IDLE;
    return 1U;
}

uint8_t camera_code_client_holds(const void *owner, uint32_t lease)
{
    return owner && s_client.owner == owner && lease && s_client.lease == lease;
}

void camera_code_client_close(const void *owner)
{
    if(!owner || s_client.owner != owner)
        return;
    s_client.owner = NULL;
    s_client.lease = 0U;
    s_client.phase = CODE_RETIRING;
    (void)resource_cleanup_schedule(RESOURCE_CLEANUP_CAMERA_CODE, finish_close);
}

hk_result_t camera_code_client_begin(const void *owner, uint32_t lease, vision_source_t source)
{
    if(source != VISION_SOURCE_QR && source != VISION_SOURCE_APRILTAG)
        return HK_ERR_INVALID_ARGUMENT;
    if(!camera_code_pass_count(source))
        return HK_ERR_FEATURE_UNAVAILABLE;
    if(s_client.phase != CODE_IDLE || (s_client.owner && s_client.owner != owner))
        return HK_ERR_BUSY;
    camera_preview_frame_t frame;
    hk_result_t result = camera_client_get(owner, lease, &frame);
    if(result != HK_OK)
        return result;
    if(!core1_executor_init())
        return HK_ERR_IO;
    result = hk_time_deadline_after_us(hk_time_service(), CODE_SCAN_TIMEOUT_US,
                                      &s_client.deadline);
    if(result != HK_OK)
        return result;
    code_exchange_t *exchange = hk_shared_buffer_uncached(&s_exchange);
    exchange->source = source;
    s_client.ticket = core1_executor_submit(prepare_job, exchange);
    if(!s_client.ticket)
        return HK_ERR_BUSY;
    s_client.owner = owner;
    s_client.lease = lease;
    s_client.pass = 0U;
    s_client.phase = CODE_PREPARING;
    return HK_OK;
}

static hk_result_t poll_scan(vision_result_snapshot_t *snapshot)
{
    uint64_t now;
    hk_result_t result = hk_time_now_us(hk_time_service(), &now);
    if(result != HK_OK)
        return result;
    if(now >= s_client.deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    if(s_client.ticket && !core1_executor_complete(s_client.ticket))
        return HK_PENDING;
    s_client.ticket = 0U;
    code_exchange_t *exchange = hk_shared_buffer_uncached(&s_exchange);
    if(s_client.phase == CODE_PREPARING && exchange->prepare_result != HK_OK)
        return exchange->prepare_result;
    if(s_client.phase == CODE_DECODING &&
       (exchange->snapshot.count || ++s_client.pass == camera_code_pass_count(exchange->source)))
    {
        *snapshot = exchange->snapshot;
        s_client.phase = CODE_IDLE;
        s_client.lease = 0U;
        return HK_OK;
    }
    s_client.phase = CODE_STAGING;
    if(!core1_executor_idle())
        return HK_PENDING;
    result = camera_code_stage(s_client.owner, s_client.lease, exchange->source,
                               s_client.pass, exchange->image, &exchange->frame);
    if(result != HK_OK)
        return result;
    s_client.ticket = core1_executor_submit(decode_job, exchange);
    if(s_client.ticket)
        s_client.phase = CODE_DECODING;
    return HK_PENDING;
}

hk_result_t camera_code_client_poll(const void *owner, vision_result_snapshot_t *snapshot)
{
    if(!snapshot)
        return HK_ERR_INVALID_ARGUMENT;
    if(!owner || s_client.owner != owner || !s_client.lease)
        return HK_ERR_INVALID_STATE;
    hk_result_t result = poll_scan(snapshot);
    if(result < 0)
        camera_code_client_close(owner);
    return result;
}
#else
hk_result_t camera_code_client_begin(const void *owner, uint32_t lease, vision_source_t source)
{
    (void)owner;
    (void)lease;
    (void)source;
    return HK_ERR_FEATURE_UNAVAILABLE;
}

hk_result_t camera_code_client_poll(const void *owner, vision_result_snapshot_t *snapshot)
{
    (void)owner;
    (void)snapshot;
    return HK_ERR_FEATURE_UNAVAILABLE;
}

uint8_t camera_code_client_holds(const void *owner, uint32_t lease)
{
    (void)owner;
    (void)lease;
    return 0U;
}

void camera_code_client_close(const void *owner)
{
    (void)owner;
}
#endif
