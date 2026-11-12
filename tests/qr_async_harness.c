#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <hackylens/capability/vision.h>

#include "../firmware/src/apps/qr_camera/qr_camera_firmware.h"
#include "../firmware/src/apps/qr_camera/qr_result.h"
#include "../firmware/src/apps/qr_camera/qr_service.h"
#include "../firmware/src/services/core1_executor.h"
#include "../firmware/src/services/resource_cleanup.h"
#include "qr_fixture.h"

static core1_executor_job_fn s_job;
static void *s_context;
static uint32_t s_ticket;
static uint32_t s_completed;
static uint64_t s_now = 1000U;
static uint8_t s_frozen;
static uint8_t s_snapshot;
static uint8_t s_stall_wait;
static uint16_t s_pixels[QR_AUTOSCAN_W * QR_AUTOSCAN_H];
static uint16_t s_vga_pixels[QR_AUTOSCAN_W * QR_AUTOSCAN_H * 4U];

static void run_worker(void)
{
    assert(s_job);
    s_job(s_context);
    s_job = NULL;
    s_completed = s_ticket;
}

uint8_t core1_executor_init(void) { return 1U; }
uint8_t core1_executor_idle(void) { return s_job == NULL; }
uint32_t core1_executor_submit(core1_executor_job_fn job, void *context)
{
    assert(!s_job);
    s_job = job;
    s_context = context;
    return ++s_ticket;
}
uint8_t core1_executor_complete(uint32_t ticket) { return s_completed >= ticket; }
uint8_t core1_executor_wait(uint32_t ticket, uint64_t timeout_us)
{
    assert(timeout_us);
    if(s_job && !s_stall_wait)
        run_worker();
    return core1_executor_complete(ticket);
}

void *hk_shared_buffer_uncached(void *buffer) { return buffer; }
const hk_time_t *hk_time_service(void) { return NULL; }
hk_result_t hk_time_now_us(const hk_time_t *time, uint64_t *value)
{ (void)time; *value = s_now; return HK_OK; }
uint8_t settings_qr_decode_rate(void) { return 5U; }
void settings_set_qr_decode_rate(uint8_t rate) { (void)rate; }
void settings_mark_dirty(uint8_t immediate) { (void)immediate; }
uint8_t camera_service_have_frame(void) { return 1U; }
uint8_t camera_service_capture_ready(void) { return !s_frozen; }
void camera_service_freeze(uint8_t frozen) { s_frozen = frozen; }
void camera_input_ignore_until_release(uint8_t held) { (void)held; }
uint8_t camera_service_frame_snapshot(uint32_t wait_ms, const uint8_t **data,
                                    uint32_t *bytes, uint16_t *width, uint16_t *height)
{
    assert(wait_ms && !s_snapshot && !bytes);
    s_snapshot = 1U;
    *data = (const uint8_t *)s_pixels;
    *width = QR_AUTOSCAN_W;
    *height = QR_AUTOSCAN_H;
    return 1U;
}
void camera_service_frame_snapshot_done(uint8_t resume)
{ assert(s_snapshot && resume); s_snapshot = 0U; }

static void draw_code(void)
{
    draw_qr_fixture(s_pixels, QR_AUTOSCAN_W, QR_AUTOSCAN_H, 0U);
}

static void submit_frame(void)
{
    qr_service_submit_frame(s_pixels, QR_AUTOSCAN_W, QR_AUTOSCAN_H, 1U, NULL);
    assert(!s_frozen && !s_snapshot);
}

int main(void)
{
    vision_result_snapshot_t snapshot;
    qr_service_enter();
    submit_frame();
    assert(qr_service_poll_decode() == QR_DECODE_IDLE);
    run_worker(); /* Prepare. */
    draw_code();
    for(unsigned y = 0U; y < QR_AUTOSCAN_H * 2U; y++)
        for(unsigned x = 0U; x < QR_AUTOSCAN_W * 2U; x++)
            s_vga_pixels[y * QR_AUTOSCAN_W * 2U + x] = s_pixels[y / 2U * QR_AUTOSCAN_W + x / 2U];
    qr_service_submit_frame(s_vga_pixels, QR_AUTOSCAN_W * 2U, QR_AUTOSCAN_H * 2U, 1U, NULL);
    assert(qr_service_poll_decode() == QR_DECODE_IDLE);
    memset(s_vga_pixels, 255, sizeof(s_vga_pixels)); /* The camera may reuse its slot. */
    run_worker();
    assert(qr_service_poll_decode() == QR_DECODE_FOUND);
    assert(strcmp(qr_result_payload_text(), "HL") == 0);
    hk_vision_read(&snapshot);
    assert(snapshot.frame_id == 1U && snapshot.source == VISION_SOURCE_QR);
    assert(snapshot.width == 640U && snapshot.height == 480U && snapshot.count == 1U);
    assert(snapshot.qr_length == 2U && memcmp(snapshot.qr_payload, "HL", 2U) == 0);
    const vision_result_item_t *box = &snapshot.items[0];
    assert(box->kind == VISION_ITEM_BLOCK && box->x0 >= 174U && box->x0 <= 178U);
    assert(box->y0 >= 94U && box->y0 <= 98U && box->x1 >= 342U && box->x1 <= 346U);
    assert(box->y1 >= 262U && box->y1 <= 266U);

    s_now += 200000U;
    memset(s_pixels, 255, sizeof(s_pixels));
    submit_frame();
    run_worker();
    assert(qr_service_poll_decode() == QR_DECODE_NOT_FOUND);
    assert(!qr_result_has_payload());
    hk_vision_read(&snapshot);
    assert(snapshot.frame_id == 2U && snapshot.source == VISION_SOURCE_QR &&
           snapshot.count == 0U && snapshot.qr_length == 0U);

    draw_code();
    assert(qr_service_decode_force() == QR_DECODE_FOUND);
    assert(!s_snapshot && !s_frozen);
    hk_vision_read(&snapshot);
    assert(snapshot.frame_id == 3U && snapshot.count == 1U && snapshot.width == QR_AUTOSCAN_W);
    s_stall_wait = 1U;
    assert(qr_service_decode_force() == QR_DECODE_DECODER_FAIL);
    assert(!s_snapshot && !s_frozen && s_job);
    qr_service_exit();
    resource_cleanup_poll();
    run_worker(); /* A timed-out decode may still complete after exit. */
    resource_cleanup_poll(); /* Queues destruction only after completion. */
    qr_service_enter();
    submit_frame();
    assert(qr_service_poll_decode() == QR_DECODE_IDLE);
    assert(!qr_result_has_payload());
    hk_vision_read(&snapshot);
    assert(snapshot.frame_id == 4U && snapshot.source == VISION_SOURCE_NONE &&
           snapshot.count == 0U && snapshot.qr_length == 0U);
    run_worker(); /* Destruction. */
    resource_cleanup_poll();
    submit_frame();
    run_worker(); /* The new session gets a fresh image. */
    qr_service_exit();
    run_worker();
    resource_cleanup_poll();
    puts("QR_ASYNC_OK");
    return 0;
}
