#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../firmware/src/drivers/camera_stream.c"
#include "../firmware/src/services/camera_capture.h"

#define CHECK(condition) do { if(!(condition)) { \
    fprintf(stderr, "FAIL line=%d: %s\n", __LINE__, #condition); \
    exit(1); } } while(0)

static hal_dvp_event_callback_t s_callback;
static void *s_callback_context;
static uint16_t s_pixels[FRAME_POOL_CAMERA_SLOT_COUNT][8];
static uint8_t s_reserved;
static camera_input_format_t s_configured_format;
static uint32_t s_dvp_status;
static uint32_t s_dvp_stops;
static uint64_t s_now_us;

uint8_t frame_pool_camera_reserve(uint32_t frame_bytes)
{
    if(s_reserved)
        return 0U;
    CHECK(frame_bytes == 320U * 240U * 2U);
    s_reserved = 1U;
    return 1U;
}
void frame_pool_camera_release(void) { s_reserved = 0U; }
uint16_t *frame_pool_camera_slot(uint8_t index)
{
    return s_reserved && index < FRAME_POOL_CAMERA_SLOT_COUNT
        ? s_pixels[index] : NULL;
}
uint32_t frame_pool_camera_frame_bytes(void) { return sizeof(s_pixels[0]); }

uint32_t hal_dvp_status(void) { return s_dvp_status; }
uint8_t hal_dvp_status_has_frame_finish(uint32_t status) { return status == 1U; }
void hal_dvp_config_rgb565(uint16_t width, uint16_t height,
    uint32_t display_addr, uint8_t burst, camera_input_format_t input_format)
{
    (void)width; (void)height; (void)display_addr; (void)burst;
    s_configured_format = input_format;
}
void hal_dvp_set_display_addr(uint32_t address) { (void)address; }
void hal_dvp_start_convert(void) {}
void hal_dvp_stop_capture(void) { s_dvp_stops++; s_dvp_status = 0U; }
uint8_t hal_dvp_irq_start(hal_dvp_event_callback_t callback, void *context)
{
    s_callback = callback;
    s_callback_context = context;
    return 1U;
}
void hal_dvp_irq_stop(void) { s_callback = NULL; }
void hal_dvp_irq_mask(void) {}
void hal_dvp_irq_unmask(void) {}
void hal_dvp_clear_frame_start(void) {}
void hal_dvp_clear_frame_finish(void) { s_dvp_status = 0U; }
uint64_t hal_time_us(void) { return s_now_us; }
void hal_sleep_ms(uint32_t ms) { s_now_us += (uint64_t)ms * 1000U; }

static void capture_frame(void)
{
    CHECK(s_callback != NULL);
    if(g_sync_pending)
        s_callback(HAL_DVP_EVENT_FRAME_START, 0U, s_callback_context);
    s_callback(HAL_DVP_EVENT_FRAME_START, 0U, s_callback_context);
    CHECK(g_capture_slot >= 0);
    s_callback(HAL_DVP_EVENT_FRAME_FINISH, 0U, s_callback_context);
}

static void first_frame_has_time_to_settle(void)
{
    s_now_us = 1000U;
    CHECK(camera_capture_start(320U, 240U, 1U, CAMERA_INPUT_RGB565));
    s_now_us += 150000U;
    CHECK(!camera_capture_tick(NULL));
    CHECK(!camera_capture_consume_timeout());
    s_now_us += 150000U;
    CHECK(!camera_capture_tick(NULL));
    CHECK(camera_capture_consume_timeout());
    capture_frame();
    CHECK(camera_capture_tick(NULL));
    CHECK(!camera_capture_consume_timeout());
    camera_capture_reset_session();
}

static void stalled_capture_recovers(void)
{
    camera_capture_frame_t leased = {0};
    camera_capture_frame_t next = {0};
    camera_capture_status_t status;
    uint32_t stops;

    s_now_us = 1000U;
    CHECK(camera_capture_start(320U, 240U, 1U, CAMERA_INPUT_RGB565));
    capture_frame();
    CHECK(camera_capture_acquire(&leased));
    s_callback(HAL_DVP_EVENT_FRAME_START, 0U, s_callback_context);
    CHECK(camera_capture_tick(NULL));
    stops = s_dvp_stops;
    for(unsigned i = 0; i < 12U; ++i)
        s_callback(HAL_DVP_EVENT_FRAME_START, 0U, s_callback_context);
    s_now_us += 149000U;
    CHECK(camera_capture_tick(NULL) && s_dvp_stops == stops);
    s_now_us += 1000U;
    CHECK(camera_capture_tick(NULL) && s_dvp_stops == stops + 1U);
    CHECK(g_capture_slot < 0 && g_sync_pending && camera_stream_has_leases());
    capture_frame();
    CHECK(camera_capture_acquire(&next));
    CHECK(next.sequence == leased.sequence + 1U && next.pixels != leased.pixels);
    camera_capture_release(next.lease_id);

    /* A pending hardware FINISH is publishable even without its IRQ. */
    s_callback(HAL_DVP_EVENT_FRAME_START, 0U, s_callback_context);
    CHECK(camera_capture_tick(NULL));
    s_dvp_status = 1U;
    s_now_us += 150000U;
    CHECK(camera_capture_tick(NULL) && s_dvp_stops == stops + 1U);
    CHECK(camera_capture_acquire(&next) && next.sequence == leased.sequence + 2U);
    camera_capture_release(next.lease_id);
    camera_capture_release(leased.lease_id);
    camera_capture_status(&status);
    CHECK(status.captured_count == 3U && status.timeout_count == 2U);
    camera_capture_reset_session();
}

int main(void)
{
    camera_stream_frame_t old_frame = {0}, new_frame = {0}, max_frame = {0};
    camera_stream_frame_t rejected = {(const volatile uint16_t *)1U, 77U, 88U};
    uint8_t leased_slot = FRAME_POOL_CAMERA_SLOT_COUNT;
    uint8_t ready_slot = FRAME_POOL_CAMERA_SLOT_COUNT;

    CHECK(!camera_stream_start(320U, 240U, 1U, (camera_input_format_t)99));
    CHECK(!s_reserved);
    CHECK(camera_stream_start(320U, 240U, 1U, CAMERA_INPUT_YUV422));
    CHECK(s_configured_format == CAMERA_INPUT_YUV422);
    camera_stream_stop();
    CHECK(camera_stream_start(320U, 240U, 1U, CAMERA_INPUT_RGB565));
    CHECK(s_configured_format == CAMERA_INPUT_RGB565);
    capture_frame();
    CHECK(!camera_stream_has_leases());
    CHECK(camera_stream_acquire_latest(&old_frame));
    CHECK(camera_stream_has_leases());
    CHECK(old_frame.lease_id != 0U);
    camera_stream_stop();

    CHECK(camera_stream_start(320U, 240U, 1U, CAMERA_INPUT_RGB565));
    capture_frame();
    CHECK(camera_stream_acquire_latest(&new_frame));
    CHECK(new_frame.lease_id > old_frame.lease_id);
    for(uint8_t index = 0U; index < FRAME_POOL_CAMERA_SLOT_COUNT; index++)
        if(g_slots[index].lease_id == new_frame.lease_id)
            leased_slot = index;
    CHECK(leased_slot < FRAME_POOL_CAMERA_SLOT_COUNT);
    camera_stream_release(old_frame.lease_id);
    CHECK(g_slots[leased_slot].state == CAMERA_SLOT_LEASED);
    CHECK(g_slots[leased_slot].lease_id == new_frame.lease_id);
    camera_stream_release(new_frame.lease_id);
    CHECK(!camera_stream_has_leases());
    CHECK(g_slots[leased_slot].state == CAMERA_SLOT_FREE);

    first_frame_has_time_to_settle();
    stalled_capture_recovers();
    CHECK(camera_stream_start(320U, 240U, 1U, CAMERA_INPUT_RGB565));
    g_next_lease_id = UINT32_MAX - 1U;
    capture_frame();
    CHECK(camera_stream_acquire_latest(&max_frame));
    CHECK(max_frame.lease_id == UINT32_MAX);
    camera_stream_release(max_frame.lease_id);
    capture_frame();
    for(uint8_t index = 0U; index < FRAME_POOL_CAMERA_SLOT_COUNT; index++)
        if(g_slots[index].state == CAMERA_SLOT_READY)
            ready_slot = index;
    CHECK(ready_slot < FRAME_POOL_CAMERA_SLOT_COUNT);
    CHECK(!camera_stream_acquire_latest(&rejected));
    CHECK(rejected.lease_id == 77U && rejected.sequence == 88U);
    CHECK(g_slots[ready_slot].state == CAMERA_SLOT_READY);
    camera_stream_stop();
    CHECK(camera_stream_start(320U, 240U, 1U, CAMERA_INPUT_RGB565));
    capture_frame();
    CHECK(!camera_stream_acquire_latest(&rejected));
    camera_stream_stop();
    puts("CAMERA_STREAM_LEASE_OK");
    return 0;
}
