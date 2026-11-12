#include "qr_service.h"

#include <stdio.h>

#include "../../core/shared_buffer.h"
#include "../../services/core1_executor.h"
#include "../../services/resource_cleanup.h"
#include "../../services/vision_result_service.h"
#include "qr_camera_frame_adapter.h"
#include "qr_camera_firmware.h"
#include "../../services/qr_decoder_engine.h"
#include "../../services/qr_luma.h"
#include "qr_result.h"

#define QR_WORKER_TIMEOUT_US 500000ULL

_Static_assert(QR_PAYLOAD_MAX - 1U == VISION_RESULT_MAX_QR_BYTES,
               "QR decoder and vision results must retain the same payload size");

typedef struct
{
    uint8_t *image;
    uint16_t width;
    uint16_t height;
    qr_engine_scan_t scan;
} qr_worker_exchange_t;

static qr_worker_exchange_t g_exchange_storage __attribute__((aligned(64)));
static qr_decoder_engine_t g_decoder;
static uint32_t g_qr_ticket;
static uint8_t g_decoder_open;
static uint8_t g_cleanup_pending;
static uint8_t g_scan_pending;
static uint16_t g_scan_width;
static uint16_t g_scan_height;
static uint64_t g_next_scan_us;
static uint64_t g_scan_started_us;
static uint32_t g_decode_count;
static int g_code_count;
static uint8_t g_last_decode_ok;
static qr_luma_stats_t g_luma;

static qr_worker_exchange_t *qr_exchange(void)
{
    return hk_shared_buffer_uncached(&g_exchange_storage);
}

static uint64_t qr_time_now_us(void)
{
    uint64_t value = 0U;
    (void)hk_time_now_us(hk_time_service(), &value);
    return value;
}

static void qr_worker_prepare(void *context)
{
    qr_worker_exchange_t *exchange = context;
    exchange->image = hk_shared_buffer_uncached(
        qr_decoder_engine_prepare(&g_decoder, exchange->width, exchange->height));
}

static void qr_worker_scan(void *context)
{
    qr_worker_exchange_t *exchange = context;
    qr_decoder_engine_scan(&g_decoder, exchange->image, &exchange->scan);
}

static void qr_worker_close(void *context)
{
    qr_worker_exchange_t *exchange = context;
    qr_decoder_engine_close(&g_decoder);
    exchange->image = NULL;
}

static uint8_t qr_worker_ready(void)
{
    if(g_qr_ticket && !core1_executor_complete(g_qr_ticket))
        return 0U;
    g_qr_ticket = 0U;
    return 1U;
}

static uint8_t qr_finish_cleanup(void)
{
    if(!g_cleanup_pending)
        return 1U;
    if(!qr_worker_ready())
        return 0U;
    if(g_decoder_open)
    {
        g_qr_ticket = core1_executor_submit(qr_worker_close, qr_exchange());
        if(g_qr_ticket)
            g_decoder_open = 0U;
        return 0U;
    }
    g_cleanup_pending = 0U;
    return 1U;
}

void qr_service_enter(void)
{
    resource_cleanup_poll();
    g_next_scan_us = 0U;
    g_decode_count = 0U;
    g_code_count = 0;
    g_last_decode_ok = 0U;
    g_luma = (qr_luma_stats_t){0};
    qr_result_reset();
    if(!core1_executor_init())
        qr_result_set_status("NO WORKER");
}

void qr_service_exit(void)
{
    g_scan_pending = 0U;
    vision_result_clear(VISION_SOURCE_QR);
    g_cleanup_pending = g_decoder_open || g_qr_ticket;
    if(!qr_finish_cleanup())
        (void)resource_cleanup_schedule(RESOURCE_CLEANUP_QR, qr_finish_cleanup);
}

void qr_service_discard_scan(void)
{
    g_scan_pending = 0U;
}

static uint8_t qr_prepare(uint16_t width, uint16_t height)
{
    qr_worker_exchange_t *exchange = qr_exchange();

    exchange->image = NULL;
    exchange->width = width;
    exchange->height = height;
    g_qr_ticket = core1_executor_submit(qr_worker_prepare, exchange);
    if(!g_qr_ticket)
        return 0U;
    g_decoder_open = 1U;
    return 1U;
}

static uint8_t qr_submit_scan(const volatile uint16_t *pixels, uint16_t width,
                              uint16_t height, uint8_t mode, uint8_t update_stats)
{
    qr_worker_exchange_t *exchange = qr_exchange();

    qr_luma_fill_image(pixels, exchange->image, exchange->width, exchange->height,
                       width, 0U, 0U, width, height, mode, update_stats, &g_luma);
    g_scan_width = width;
    g_scan_height = height;
    g_qr_ticket = core1_executor_submit(qr_worker_scan, exchange);
    return g_qr_ticket != 0U;
}

void qr_service_submit_frame(const volatile uint16_t *pixels, uint16_t width,
                             uint16_t height, uint32_t sequence, void *context)
{
    qr_worker_exchange_t *exchange = qr_exchange();
    uint64_t now = qr_time_now_us();

    (void)sequence;
    (void)context;
    if(now < g_next_scan_us || g_cleanup_pending || g_scan_pending ||
       !qr_worker_ready() || !core1_executor_idle())
        return;
    if(!exchange->image || exchange->width != QR_AUTOSCAN_W ||
       exchange->height != QR_AUTOSCAN_H)
    {
        if(!exchange->image && g_decoder_open &&
           exchange->width == QR_AUTOSCAN_W && exchange->height == QR_AUTOSCAN_H)
        {
            qr_result_set_status("NO BUFFER");
            g_next_scan_us = now + 1000000ULL;
        }
        (void)qr_prepare(QR_AUTOSCAN_W, QR_AUTOSCAN_H);
        return;
    }
    g_scan_started_us = now;
    if(qr_submit_scan(pixels, width, height, 0U, 1U))
    {
        g_scan_pending = 1U;
        g_next_scan_us = now + 1000000ULL / settings_qr_decode_rate();
    }
}

static void qr_publish_results(void)
{
    const qr_worker_exchange_t *exchange = qr_exchange();
    const qr_engine_scan_t *scan = &exchange->scan;

    if(scan->found)
    {
        const vision_result_item_t block = {
            .kind = VISION_ITEM_BLOCK,
            .x0 = (uint32_t)scan->x0 * g_scan_width / exchange->width,
            .y0 = (uint32_t)scan->y0 * g_scan_height / exchange->height,
            .x1 = (uint32_t)scan->x1 * g_scan_width / exchange->width,
            .y1 = (uint32_t)scan->y1 * g_scan_height / exchange->height,
        };
        vision_result_publish_qr(g_scan_width, g_scan_height, &block,
                                 scan->payload, scan->payload_len);
    }
    else
        vision_result_publish(VISION_SOURCE_QR, g_scan_width, g_scan_height, NULL, 0U);
}

static qr_decode_result_t qr_update_result(const char *mode)
{
    const qr_engine_scan_t *scan = &qr_exchange()->scan;

    if(scan->code_count > g_code_count)
        g_code_count = scan->code_count;
    g_last_decode_ok = scan->found;
    if(scan->found)
    {
        qr_result_set_payload(scan->payload, scan->payload_len);
        qr_result_set_status(mode);
        return QR_DECODE_FOUND;
    }
    qr_result_clear_payload();
    qr_result_set_status(g_code_count ? "NO DATA" : "NO QR");
    return QR_DECODE_NOT_FOUND;
}

qr_decode_result_t qr_service_poll_decode(void)
{
    qr_decode_result_t result;

    if(g_cleanup_pending || !qr_worker_ready() || !g_scan_pending)
        return QR_DECODE_IDLE;
    g_scan_pending = 0U;
    g_code_count = 0;
    g_decode_count++;
    result = qr_update_result("raw");
    qr_publish_results();
    printf("[QR] autoscan elapsed_us=%u result=%d\r\n",
           (unsigned)(qr_time_now_us() - g_scan_started_us), (int)result);
    return result;
}

static uint8_t qr_wait_worker(void)
{
    if(g_qr_ticket && !core1_executor_wait(g_qr_ticket, QR_WORKER_TIMEOUT_US))
        return 0U;
    return qr_worker_ready();
}

qr_decode_result_t qr_service_decode_force(void)
{
    static const char *const labels[] = {"raw", "invert", "stretch", "stretch-invert"};
    qr_camera_decode_frame_t frame;
    qr_decode_result_t result = QR_DECODE_DECODER_FAIL;
    qr_worker_exchange_t *exchange = qr_exchange();

    qr_service_discard_scan();
    if(g_cleanup_pending || !qr_wait_worker() || !core1_executor_idle())
    {
        qr_result_set_status("WORKER BUSY");
        return result;
    }
    if(!camera_service_capture_ready() || !qr_camera_frame_acquire(1U, &frame))
    {
        qr_result_set_status("NO FRAME");
        return QR_DECODE_NO_FRAME;
    }

    if(!exchange->image || exchange->width != frame.width || exchange->height != frame.height)
    {
        if(!qr_prepare(frame.width, frame.height) || !qr_wait_worker())
        {
            qr_result_set_status("WORKER BUSY");
            goto done;
        }
        if(!exchange->image)
        {
            qr_result_set_status("NO BUFFER");
            goto done;
        }
    }
    g_decode_count++;
    g_code_count = 0;
    g_last_decode_ok = 0U;
    for(uint8_t pass = 0U; pass < sizeof(labels) / sizeof(labels[0]); pass++)
    {
        if(!qr_submit_scan(frame.pixels, frame.width, frame.height, pass, pass == 0U) ||
           !qr_wait_worker())
        {
            qr_result_set_status("WORKER BUSY");
            result = QR_DECODE_DECODER_FAIL;
            break;
        }
        result = qr_update_result(labels[pass]);
        if(result == QR_DECODE_FOUND)
            break;
    }
    if(result == QR_DECODE_FOUND || result == QR_DECODE_NOT_FOUND)
        qr_publish_results();

done:
    g_next_scan_us = qr_time_now_us() + 1000000ULL / settings_qr_decode_rate();
    qr_camera_frame_resume_if_needed(frame.using_snapshot);
    return result;
}

void qr_service_format_info(char *line, size_t line_size, const char *screen)
{
    uint8_t ready = !g_cleanup_pending && !g_qr_ticket && qr_exchange()->image != NULL;

    snprintf(line, line_size,
             "HKQRINFO screen=%s ready=%u frame=%u rate=%u decodes=%u codes=%d status=%s ok=%u payload=\"%s\" luma=%u/%u/%u\r\n",
             screen, ready, camera_service_have_frame(), settings_qr_decode_rate(),
             (unsigned)g_decode_count, g_code_count, qr_result_status(), g_last_decode_ok,
             qr_result_has_payload() ? qr_result_payload_text() : "",
             g_luma.min, g_luma.avg, g_luma.max);
}

uint8_t qr_service_decode_rate(void)
{
    return settings_qr_decode_rate();
}

void qr_service_set_decode_rate(uint8_t rate)
{
    settings_set_qr_decode_rate(rate);
    g_next_scan_us = 0U;
}

uint8_t qr_service_cycle_decode_rate(void)
{
    uint8_t rate = settings_qr_decode_rate();
    rate = rate >= QR_DECODE_RATE_MAX ? QR_DECODE_RATE_MIN : (uint8_t)(rate + 1U);
    qr_service_set_decode_rate(rate);
    settings_mark_dirty(1);
    return rate;
}
