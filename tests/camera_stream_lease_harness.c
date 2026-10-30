#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../firmware/src/drivers/camera_stream.c"

#define CHECK(condition) do { if(!(condition)) { \
    fprintf(stderr, "FAIL line=%d: %s\n", __LINE__, #condition); \
    exit(1); } } while(0)

static hal_dvp_event_callback_t s_callback;
static void *s_callback_context;
static uint16_t s_pixels[FRAME_POOL_CAMERA_SLOT_COUNT][8];
static uint8_t s_reserved;
static camera_input_format_t s_configured_format;

uint8_t frame_pool_camera_reserve(void)
{
    if(s_reserved)
        return 0U;
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

uint32_t hal_dvp_status(void) { return 0U; }
void hal_dvp_config_rgb565(uint16_t width, uint16_t height,
    uint32_t display_addr, uint8_t burst, camera_input_format_t input_format)
{
    (void)width; (void)height; (void)display_addr; (void)burst;
    s_configured_format = input_format;
}
void hal_dvp_set_display_addr(uint32_t address) { (void)address; }
void hal_dvp_start_convert(void) {}
void hal_dvp_stop_capture(void) {}
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

static void capture_frame(void)
{
    CHECK(s_callback != NULL);
    if(g_sync_pending)
        s_callback(HAL_DVP_EVENT_FRAME_START, 0U, s_callback_context);
    s_callback(HAL_DVP_EVENT_FRAME_START, 0U, s_callback_context);
    CHECK(g_capture_slot >= 0);
    s_callback(HAL_DVP_EVENT_FRAME_FINISH, 0U, s_callback_context);
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
