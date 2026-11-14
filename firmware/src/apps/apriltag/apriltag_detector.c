#include "../../services/resource_cleanup.h"
#include "apriltag_detector.h"

#include <stdio.h>
#include <string.h>

#include <hackylens/capability/time.h>

#include "apriltag.h"
#include "../../services/apriltag_decoder.h"

#include "../../services/core1_executor.h"
#include "../../core/shared_buffer.h"
#include "apriltag_config.h"

typedef enum
{
    APRILTAG_WORKER_OFF = 0,
    APRILTAG_WORKER_STARTING,
    APRILTAG_WORKER_READY,
    APRILTAG_WORKER_BUSY,
    APRILTAG_WORKER_STOPPING,
    APRILTAG_WORKER_ERROR,
} apriltag_worker_state_t;

typedef struct
{
    uint32_t epoch;
    uint8_t count;
    apriltag_result_t items[APRILTAG_RESULT_MAX];
} apriltag_result_bank_t;

static apriltag_decoder_t g_decoder;
static apriltag_result_bank_t g_result_banks[2] __attribute__((aligned(64)));

typedef enum
{
    APRILTAG_JOB_NONE = 0,
    APRILTAG_JOB_CREATE,
    APRILTAG_JOB_DETECT,
    APRILTAG_JOB_DESTROY,
} apriltag_job_t;

static volatile uint8_t g_active;
static volatile uint8_t g_cleanup_pending;
static volatile uint8_t g_worker_state;
static volatile uint8_t g_job_kind;
static volatile uint32_t g_job_ticket;
static volatile uint32_t g_session_epoch;
static volatile uint32_t g_request_sequence;
static volatile uint32_t g_complete_sequence;
static volatile uint32_t g_published_sequence;
static volatile uint32_t g_request_epoch;
static volatile uint16_t g_request_width;
static volatile uint16_t g_request_height;
static apriltag_scan_region_t g_request_region;
static uint8_t g_request_native;
static volatile uint8_t g_last_decimate = 2U;
static volatile uint16_t g_last_input_width = APRILTAG_INPUT_W;
static volatile uint16_t g_last_input_height = APRILTAG_INPUT_H;
static volatile uint32_t g_frame_count;
static volatile uint32_t g_submit_count;
static volatile uint32_t g_busy_drop_count;
static volatile uint32_t g_last_us;
static volatile uint32_t g_last_quads;
static volatile uint32_t g_last_preprocess_us;
static volatile uint32_t g_last_quad_search_us;
static volatile uint32_t g_last_decode_total_us;
static volatile uint32_t g_last_refine_us;
static volatile uint32_t g_last_homography_us;
static volatile uint32_t g_last_payload_us;
static volatile uint32_t g_last_result_us;
static volatile uint32_t g_last_reconcile_us;
static volatile uint32_t g_last_cleanup_us;
static volatile uint8_t g_refine_edges_requested = APRILTAG_REFINE_EDGES;


static uint64_t apriltag_time_now_us(void)
{
    uint64_t value = 0U;

    if(hk_time_now_us(hk_time_service(), &value) != HK_OK)
        return 0U;
    return value;
}

static hk_result_t apriltag_time_deadline_after_us(
    uint64_t duration_us, hk_deadline_t *deadline)
{

    return hk_time_deadline_after_us(
        hk_time_service(), duration_us, deadline);
}

static void apriltag_time_sleep_us(uint64_t duration_us)
{
    hk_deadline_t wake;

    if(hk_time_deadline_after_us(
           hk_time_service(), duration_us, &wake) != HK_OK)
        return;
    (void)hk_time_sleep_until(
        hk_time_service(), wake, wake, NULL);
}

static void *shared_uncached(void *pointer)
{
    return hk_shared_buffer_uncached(pointer);
}


static apriltag_result_bank_t *shared_result_bank(uint8_t index)
{
    return (apriltag_result_bank_t *)shared_uncached(&g_result_banks[index & 1U]);
}

static void detector_run(uint32_t request_sequence, uint32_t request_epoch,
                         uint16_t width, uint16_t height)
{
    uint32_t publish_sequence = g_published_sequence + 1U;
    if(publish_sequence == 0U)
        publish_sequence = 1U;
    apriltag_result_bank_t *bank = shared_result_bank((uint8_t)publish_sequence);
    bank->epoch = request_epoch;

    uint64_t started_us = apriltag_time_now_us();
    const apriltag_scan_region_t *region = g_request_native ? &g_request_region : NULL;
    bank->count = apriltag_decoder_scan(&g_decoder, width, height,
                                        g_refine_edges_requested, region, bank->items);
    g_last_decimate = region && region->full_detail ? 1U : 2U;
    g_last_input_width = region ? region->width : APRILTAG_INPUT_W;
    g_last_input_height = region ? region->height : APRILTAG_INPUT_H;
    g_last_us = (uint32_t)(apriltag_time_now_us() - started_us);
    g_last_quads = g_decoder.detector->nquads;
    g_last_preprocess_us = g_decoder.detector->profile_preprocess_us;
    g_last_quad_search_us = g_decoder.detector->profile_quad_search_us;
    g_last_decode_total_us = g_decoder.detector->profile_decode_total_us;
    g_last_refine_us = g_decoder.detector->profile_refine_us;
    g_last_homography_us = g_decoder.detector->profile_homography_us;
    g_last_payload_us = g_decoder.detector->profile_payload_us;
    g_last_result_us = g_decoder.detector->profile_result_us;
    g_last_reconcile_us = g_decoder.detector->profile_reconcile_us;
    g_last_cleanup_us = g_decoder.detector->profile_cleanup_us;
    g_frame_count++;

    __sync_synchronize();
    if(g_active && request_epoch == g_session_epoch)
        g_published_sequence = publish_sequence;
    g_complete_sequence = request_sequence;
}

static void apriltag_worker_job(void *context)
{
    apriltag_job_t job = (apriltag_job_t)g_job_kind;

    (void)context;
    if(job == APRILTAG_JOB_CREATE)
    {
        g_worker_state = APRILTAG_WORKER_STARTING;
        if(!apriltag_decoder_open(&g_decoder))
        {
            apriltag_decoder_close(&g_decoder);
            g_worker_state = APRILTAG_WORKER_ERROR;
        }
        else
            g_worker_state = APRILTAG_WORKER_READY;
    }
    else if(job == APRILTAG_JOB_DETECT)
    {
        uint32_t request_sequence = g_request_sequence;
        uint32_t request_epoch = g_request_epoch;
        uint16_t width = g_request_width;
        uint16_t height = g_request_height;

        if(g_decoder.detector)
        {
            g_worker_state = APRILTAG_WORKER_BUSY;
            detector_run(request_sequence, request_epoch, width, height);
            g_worker_state = g_active ? APRILTAG_WORKER_READY : APRILTAG_WORKER_STOPPING;
        }
    }
    else if(job == APRILTAG_JOB_DESTROY)
    {
        g_worker_state = APRILTAG_WORKER_STOPPING;
        apriltag_decoder_close(&g_decoder);
        g_complete_sequence = g_request_sequence;
        g_worker_state = APRILTAG_WORKER_OFF;
    }
    __sync_synchronize();
}

void apriltag_detector_service_tick(void)
{
    uint32_t ticket = g_job_ticket;

    if(ticket && core1_executor_complete(ticket))
    {
        g_job_ticket = 0U;
        g_job_kind = APRILTAG_JOB_NONE;
        __sync_synchronize();
    }

    if(!g_active && g_cleanup_pending && !g_job_ticket)
    {
        if(g_decoder.detector)
        {
            g_job_kind = APRILTAG_JOB_DESTROY;
            __sync_synchronize();
            g_job_ticket = core1_executor_submit(apriltag_worker_job, NULL);
            if(!g_job_ticket)
                g_job_kind = APRILTAG_JOB_NONE;
        }
        else
        {
            g_cleanup_pending = 0U;
            g_worker_state = APRILTAG_WORKER_OFF;
        }
    }
}

static uint8_t finish_cleanup(void)
{
    apriltag_detector_service_tick();
    return !g_cleanup_pending && !g_job_ticket;
}

uint8_t apriltag_detector_init(void)
{
    hk_deadline_t deadline;

    resource_cleanup_poll();
    if(apriltag_time_deadline_after_us(
           APRILTAG_START_TIMEOUT_US, &deadline) != HK_OK)
    {
        g_worker_state = APRILTAG_WORKER_ERROR;
        return 0U;
    }

    if(!core1_executor_init())
    {
        g_worker_state = APRILTAG_WORKER_ERROR;
        return 0U;
    }
    while((g_cleanup_pending || !core1_executor_idle()) &&
          apriltag_time_now_us() < deadline.at_us)
    {
        apriltag_detector_service_tick();
        apriltag_time_sleep_us(1000U);
    }
    if(g_cleanup_pending || !core1_executor_idle())
    {
        g_worker_state = APRILTAG_WORKER_ERROR;
        return 0U;
    }
    g_session_epoch++;
    if(g_session_epoch == 0U)
        g_session_epoch++;
    g_active = 1U;
    g_cleanup_pending = 0U;
    g_worker_state = APRILTAG_WORKER_STARTING;
    g_job_kind = APRILTAG_JOB_CREATE;
    __sync_synchronize();
    g_job_ticket = core1_executor_submit(apriltag_worker_job, NULL);
    if(!g_job_ticket ||
       !core1_executor_wait(g_job_ticket, APRILTAG_START_TIMEOUT_US))
    {
        g_active = 0U;
        /*
         * A timed-out CREATE can still complete after this core returns. Keep
         * cleanup armed even when the worker has not published g_detector yet.
         */
        g_cleanup_pending = (g_job_ticket || g_decoder.detector) ? 1U : 0U;
        g_worker_state = APRILTAG_WORKER_ERROR;
        (void)resource_cleanup_schedule(RESOURCE_CLEANUP_CORE1, finish_cleanup);
        return 0U;
    }
    apriltag_detector_service_tick();
    if(g_worker_state != APRILTAG_WORKER_READY)
    {
        g_active = 0U;
        g_cleanup_pending = (g_job_ticket || g_decoder.detector) ? 1U : 0U;
        (void)resource_cleanup_schedule(RESOURCE_CLEANUP_CORE1, finish_cleanup);
        return 0U;
    }

    printf("[APRILTAG] core1 TAG36H11 %ux%u decimate=2 refine=%u hamming<=%d\r\n",
           APRILTAG_INPUT_W, APRILTAG_INPUT_H, g_refine_edges_requested,
           APRILTAG_MAX_HAMMING);
    return 1U;
}

void apriltag_detector_deinit(void)
{
    g_session_epoch++;
    if(g_session_epoch == 0U)
        g_session_epoch++;
    g_active = 0U;
    g_cleanup_pending = (g_job_ticket || g_decoder.detector) ? 1U : 0U;
    __sync_synchronize();
    apriltag_detector_service_tick();
    if(!finish_cleanup())
        (void)resource_cleanup_schedule(RESOURCE_CLEANUP_CORE1, finish_cleanup);
}

uint8_t apriltag_detector_submit(const volatile uint16_t *pixels, uint16_t width, uint16_t height,
                                 const apriltag_scan_region_t *region)
{
    uint32_t request_sequence;

    apriltag_detector_service_tick();
    if(!pixels || !width || !height || !g_active)
        return 0;
    if(region && (!region->width || !region->height ||
                  region->width > APRILTAG_INPUT_W || region->height > APRILTAG_INPUT_H ||
                  region->x + region->width > width || region->y + region->height > height))
        return 0;
    if(g_worker_state != APRILTAG_WORKER_READY ||
       g_request_sequence != g_complete_sequence || g_job_ticket)
    {
        g_busy_drop_count++;
        return 0;
    }

    request_sequence = g_request_sequence + 1U;
    if(request_sequence == 0U)
        request_sequence = 1U;
    apriltag_decoder_fill(pixels, width, height, region);

    g_request_width = width;
    g_request_height = height;
    g_request_native = region != NULL;
    if(region)
        g_request_region = *region;
    g_request_epoch = g_session_epoch;
    g_job_kind = APRILTAG_JOB_DETECT;
    __sync_synchronize();
    g_request_sequence = request_sequence;
    g_job_ticket = core1_executor_submit(apriltag_worker_job, NULL);
    if(!g_job_ticket)
    {
        g_job_kind = APRILTAG_JOB_NONE;
        g_request_sequence = g_complete_sequence;
        g_busy_drop_count++;
        return 0U;
    }
    g_submit_count++;
    return 1;
}

void apriltag_detector_set_refine_edges(uint8_t enabled)
{
    g_refine_edges_requested = enabled ? 1U : 0U;
    __sync_synchronize();
}

const apriltag_result_t *apriltag_detector_results(uint8_t *count, uint32_t *sequence)
{
    uint32_t published_sequence = g_published_sequence;
    apriltag_result_bank_t *bank;

    __sync_synchronize();
    bank = shared_result_bank((uint8_t)published_sequence);
    if(!g_active || published_sequence == 0U || bank->epoch != g_session_epoch)
    {
        if(sequence)
            *sequence = 0U;
        if(count)
            *count = 0;
        return bank->items;
    }
    if(count)
        *count = bank->count > APRILTAG_RESULT_MAX ? APRILTAG_RESULT_MAX : bank->count;
    if(sequence)
        *sequence = published_sequence;
    return bank->items;
}

void apriltag_detector_discard_results(void)
{
    if(++g_session_epoch == 0U)
        g_session_epoch++;
    __sync_synchronize();
}

void apriltag_detector_format_info(char *line, size_t line_size)
{
    static const char *const states[] = {"OFF", "STARTING", "READY", "BUSY", "STOPPING", "ERROR"};
    uint8_t state = g_worker_state;
    uint8_t count = 0;

    (void)apriltag_detector_results(&count, NULL);
    if(state >= sizeof(states) / sizeof(states[0]))
        state = APRILTAG_WORKER_ERROR;
    snprintf(line, line_size,
             "HKTAGINFO state=%s core=1 family=TAG36H11 input=%ux%u decimate=%u edge_refine=%u frames=%u submitted=%u "
             "busy_drop=%u us=%u quads=%u tags=%u pre=%u search=%u decode=%u refine=%u homo=%u "
             "payload=%u result=%u reconcile=%u cleanup=%u\r\n",
             states[state], g_last_input_width, g_last_input_height, g_last_decimate, g_refine_edges_requested,
             (unsigned)g_frame_count, (unsigned)g_submit_count, (unsigned)g_busy_drop_count,
             (unsigned)g_last_us, (unsigned)g_last_quads, (unsigned)count,
             (unsigned)g_last_preprocess_us, (unsigned)g_last_quad_search_us,
             (unsigned)g_last_decode_total_us, (unsigned)g_last_refine_us,
             (unsigned)g_last_homography_us, (unsigned)g_last_payload_us,
             (unsigned)g_last_result_us, (unsigned)g_last_reconcile_us,
             (unsigned)g_last_cleanup_us);
}
