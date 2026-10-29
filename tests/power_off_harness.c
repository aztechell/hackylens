#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/apps/power_off/power_off_app.h"
#include "../firmware/src/services/micropython_runtime.h"
#include "../firmware/src/storage/userfs.h"
#include <hackylens/capability/power.h>
static uint64_t now = 100U;
static uint32_t buttons;
static unsigned closes, stop_requests, saves, aborts, unmounts, remounts, off_calls;
static uint8_t save_ok = 1U, off_ok = 1U;
static micropython_runtime_state_t runtime_state;
static userfs_status_t filesystem = {.state = USERFS_STATE_MOUNTED};
uint64_t hal_time_us(void) { return now; }
uint8_t settings_storage_flush(void) { saves++; return save_ok; }
void micropython_runtime_get_status(micropython_runtime_status_t *status)
{ memset(status, 0, sizeof(*status)); status->state = runtime_state; }
uint8_t micropython_runtime_request_stop(void) { stop_requests++; return 1U; }
void userfs_get_status(userfs_status_t *status) { *status = filesystem; }
userfs_result_t userfs_upload_abort(uint32_t id)
{ assert(id == 7U); aborts++; filesystem.upload_id = 0U; return USERFS_OK; }
userfs_result_t userfs_unmount(void)
{ unmounts++; filesystem.state = USERFS_STATE_UNINITIALIZED; return USERFS_OK; }
userfs_result_t userfs_mount(void)
{ remounts++; filesystem.state = USERFS_STATE_MOUNTED; return USERFS_OK; }
uint8_t axp173_power_off(void)
{ assert(filesystem.state != USERFS_STATE_MOUNTED); off_calls++; return off_ok; }
const hk_time_t *hk_time_service(void) { return (const hk_time_t *)1; }
hk_result_t hk_time_deadline_after_us(const hk_time_t *time, uint64_t duration, hk_deadline_t *deadline)
{ (void)time; deadline->at_us = now + duration; return HK_OK; }
hk_result_t hk_app_context_state(const hk_app_context_t *ctx, void **data, uint32_t *size)
{ (void)ctx; *data = power_off_v2_entry.state_storage; *size = power_off_v2_entry.state_capacity_bytes; return HK_OK; }
hk_result_t hk_app_context_input(const hk_app_context_t *ctx, const hk_input_t **input)
{ (void)ctx; *input = (const hk_input_t *)1; return HK_OK; }
hk_result_t hk_input_get_state(const hk_input_t *input, uint32_t *state)
{ (void)input; *state = buttons; return HK_OK; }
hk_result_t hk_app_context_request_render(const hk_app_context_t *ctx, const hk_display_rect_t *rect)
{ (void)ctx; (void)rect; return HK_OK; }
hk_result_t hk_app_context_request_close(const hk_app_context_t *ctx)
{ (void)ctx; closes++; return HK_OK; }
hk_result_t hk_app_context_teardown_deadline(const hk_app_context_t *ctx, hk_deadline_t *deadline)
{ (void)ctx; deadline->at_us = now + 100U; return HK_OK; }
hk_result_t hk_app_surface_clear(hk_app_surface_t *surface, uint16_t color)
{ (void)surface; (void)color; return HK_OK; }
hk_result_t hk_app_surface_text(hk_app_surface_t *surface, const hk_display_rect_t *rect,
                              const char *text, uint32_t size, uint16_t color)
{ (void)surface; (void)rect; (void)text; (void)size; (void)color; return HK_OK; }
static void input(uint32_t pressed, uint32_t released)
{
    hk_app_event_t event = {.kind = HK_APP_EVENT_INPUT};
    event.data.input.pressed = pressed; event.data.input.released = released;
    assert(power_off_v2_entry.event(NULL, &event) == HK_OK);
}
static void tick(void)
{ hk_app_event_t event = {.kind = HK_APP_EVENT_TIMER}; assert(power_off_v2_entry.event(NULL, &event) == HK_OK); }
int main(void)
{
    buttons = HK_INPUT_BUTTON_OK;
    assert(power_off_v2_entry.start(NULL) == HK_OK);
    input(HK_INPUT_BUTTON_OK, 0U); tick();
    assert(!off_calls && !saves); /* Launch/held OK cannot confirm. */
    input(HK_INPUT_BUTTON_BACK, 0U);
    assert(closes == 1U && !off_calls);
    assert(power_off_v2_entry.start(NULL) == HK_OK);
    input(0U, HK_INPUT_BUTTON_OK);
    runtime_state = MICROPYTHON_RUNTIME_RUNNING;
    input(HK_INPUT_BUTTON_OK, 0U);
    assert(stop_requests == 1U && !saves && !off_calls);
    runtime_state = MICROPYTHON_RUNTIME_FINISHED;
    save_ok = 0U; tick();
    assert(!off_calls && !unmounts); /* Failed persistence prevents power cut. */
    save_ok = 1U; off_ok = 0U; filesystem.upload_id = 7U;
    input(HK_INPUT_BUTTON_OK, 0U);
    assert(aborts == 1U && unmounts == 1U && off_calls == 1U && remounts == 1U);
    off_ok = 1U;
    input(HK_INPUT_BUTTON_OK, 0U);
    assert(off_calls == 2U && unmounts == 2U);
    tick(); assert(off_calls == 2U); /* Accepted command is not repeated. */
    buttons = 0U; assert(power_off_v2_entry.start(NULL) == HK_OK);
    runtime_state = MICROPYTHON_RUNTIME_RUNNING;
    input(HK_INPUT_BUTTON_OK, 0U);
    now += 2000001U; tick();
    assert(off_calls == 2U); /* STOP timeout prevents power cut. */
    assert(hk_power_request_off(NULL, (hk_deadline_t){0}) == HK_ERR_CAPABILITY_ABSENT);
    puts("POWER_OFF_OK confirmation cancel stop flush rollback deadline");
    return 0;
}
