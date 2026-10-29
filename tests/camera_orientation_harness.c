#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../firmware/src/services/camera_session.h"
#include "../firmware/src/services/internal/camera_session_state.h"
#include "../firmware/src/services/camera_frame.h"
#include "../firmware/src/services/camera_capture.h"
#include "../firmware/src/services/camera_persist_settings.h"

static camera_size_t requested_size;
static uint16_t capture_width, capture_height;
static uint16_t pixels[1];
static unsigned released;

camera_size_t camera_service_size(void) { return requested_size; }
void camera_settings_force_size(camera_size_t size) { requested_size = size; }
uint8_t camera_settings_consume_size_pending(void) { return 0; }
void camera_input_reset(uint8_t down) { (void)down; }
void camera_input_return_from_settings(uint8_t down) { (void)down; }
void camera_input_cancel(void) {}
void camera_service_set_light_active(uint8_t active) { (void)active; }
void camera_service_set_light_level(uint8_t level) { (void)level; }
void camera_light_apply(void) {}
void camera_light_restore_global(void) {}
void camera_service_fps_reset(void) {}
void camera_fps_mark_stopped(void) {}
void camera_fps_log_summary(const char *prefix, uint16_t width, uint16_t height)
{ (void)prefix; (void)width; (void)height; }
void camera_capture_reset_session(void) {}
void camera_capture_reset_flow(void) {}
void camera_capture_pause(void) {}
void camera_capture_status(camera_capture_status_t *status)
{ memset(status, 0, sizeof(*status)); }
uint8_t camera_capture_have_frame(void) { return 1; }
uint8_t camera_capture_tick(const char *prefix) { (void)prefix; return 1; }
uint8_t camera_capture_consume_timeout(void) { return 0; }
uint8_t camera_capture_snapshot(uint16_t width, uint16_t height, uint32_t wait_ms,
                                const char *prefix)
{ (void)width; (void)height; (void)wait_ms; (void)prefix; return 1; }
void camera_capture_snapshot_done(uint8_t resume) { (void)resume; }
uint8_t camera_capture_start(uint16_t width, uint16_t height, uint8_t burst)
{ capture_width = width; capture_height = height; (void)burst; return 1; }
uint8_t camera_capture_acquire(camera_capture_frame_t *frame)
{ *frame = (camera_capture_frame_t){pixels, 1, 1}; return 1; }
void camera_capture_release(uint32_t lease_id) { assert(lease_id == 1); released++; }
const uint16_t *camera_capture_snapshot_pixels(void) { return NULL; }
void camera_sensor_init_bus(uint32_t *xclk, uint32_t *sccb)
{ *xclk = 22000000; *sccb = 100000; }
void camera_sensor_probe_id(uint16_t *mid, uint16_t *pid)
{ *mid = 0x7fa2; *pid = 0x7742; }
uint8_t camera_sensor_matches_id(uint16_t mid, uint16_t pid)
{ return mid == 0x7fa2 && pid == 0x7742; }
const char *camera_sensor_name(void) { return "FAKE"; }
void camera_sensor_apply_init_table(void) {}
void camera_sensor_apply_common_tuning(void) {}
void camera_sensor_apply_output_size(uint16_t width, uint16_t height)
{ (void)width; (void)height; }
void camera_sensor_apply_colorbar(uint8_t enabled) { (void)enabled; }
void hal_sleep_ms(uint32_t ms) { (void)ms; }
void hal_dvp_output_display(uint8_t enabled) { (void)enabled; }
void hal_dvp_output_ai(uint8_t enabled) { (void)enabled; }

int main(int argc, char **argv)
{
    const camera_size_t sizes[] = {
        CAMERA_SIZE_160X120, CAMERA_SIZE_320X240, CAMERA_SIZE_640X480,
    };
    assert(argc == 2);
    const unsigned expected_rotate = (unsigned)atoi(argv[1]);

    for(unsigned size = 0; size < 3; size++)
        for(uint8_t qr = 0; qr < 2; qr++)
            for(uint8_t qvga = 0; qvga < 2; qvga++)
            {
                camera_preview_frame_t frame;
                requested_size = sizes[size];
                const uint16_t expected_width = qr ? 640 : qvga ? 320 :
                    size == 0 ? 160 : size == 1 ? 320 : 640;
                camera_service_enter_begin(qr, 0);
                camera_service_set_qvga_mode(qvga);
                assert(camera_service_start());
                assert(camera_service_preview_acquire(&frame));
                assert(frame.rotate == expected_rotate);
                assert(frame.width == expected_width);
                assert(frame.height == expected_width * 3 / 4);
                assert(capture_width == frame.width && capture_height == frame.height);
                camera_service_preview_release(&frame);
                assert(frame.lease_id == 0);
                camera_stop();
                assert(!camera_session_ready());
                camera_service_clear_mode();
            }
    assert(released == 12);
    puts("CAMERA_ORIENTATION_OK modes=sizes/qr/ai restarts=12 capture=unchanged");
    return 0;
}
