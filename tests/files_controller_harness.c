#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/src/apps/files/files_controller.h"
#include "../firmware/src/apps/files/file_browser_mode.h"
#include "../firmware/src/core/hk_events.h"

static file_browser_mode_t g_mode;
static uint64_t g_now_us;
static uint8_t g_time_fails;
static unsigned g_now_calls;
static unsigned g_open_selected;
static unsigned g_delete_confirm;
static unsigned g_failures;

static void check(int condition, const char *message)
{
    if(!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

hk_result_t hk_time_now_us(
    hk_owner_t owner, const hk_time_t *handle, uint64_t *value)
{
    (void)owner;
    (void)handle;
    g_now_calls++;
    if(g_time_fails)
        return HK_ERR_INVALID_STATE;
    *value = g_now_us;
    return HK_OK;
}

void files_backend_enter(void) {}
void files_refresh_after_sd_event(hk_sd_event_t event) { (void)event; }
void files_nav_delta(int8_t delta) { (void)delta; }
uint8_t files_back_from_list(void) { return 0U; }
void files_open_selected(void) { g_open_selected++; }
uint8_t files_delete_confirm_enter(void)
{
    g_delete_confirm++;
    return 1U;
}
void files_delete_cancel(void) {}
void files_delete_confirmed(void) {}
void files_presenter_close_image(void) {}
void files_presenter_render_list(void) {}
void files_presenter_tick_image(uint64_t now_us) { (void)now_us; }
uint8_t files_presenter_toggle_image_pause(uint64_t now_us)
{
    (void)now_us;
    return 1U;
}
file_browser_mode_t files_mode(void) { return g_mode; }
void files_set_mode(file_browser_mode_t mode) { g_mode = mode; }

static void tap_ok(files_state_t *state)
{
    hk_input_event_t event = {0};

    event.state = HK_INPUT_BUTTON_OK;
    event.pressed = HK_INPUT_BUTTON_OK;
    event.changed = HK_INPUT_BUTTON_OK;
    files_controller_handle_input(state, &event);
    event.state = 0U;
    event.pressed = 0U;
    event.changed = HK_INPUT_BUTTON_OK;
    files_controller_handle_input(state, &event);
}

int main(void)
{
    files_state_t state;
    hk_input_event_t hold = {0};

    g_mode = FILES_MODE_LIST;
    g_now_us = 1000U;
    memset(&state, 0, sizeof(state));
    state.owner.slot = 1U;
    state.owner.generation = 1U;
    state.time.lease.slot = 1U;
    state.time.lease.generation = 1U;
    files_controller_enter(&state);
    tap_ok(&state);
    check(g_open_selected == 1U, "short OK must open selected entry");
    check(g_now_calls >= 1U, "Files must sample injected monotonic time");

    g_time_fails = 1U;
    tap_ok(&state);
    check(
        g_open_selected == 2U,
        "short OK must remain usable when timing is unavailable");

    hold.state = HK_INPUT_BUTTON_OK;
    hold.pressed = HK_INPUT_BUTTON_OK;
    hold.changed = HK_INPUT_BUTTON_OK;
    files_controller_handle_input(&state, &hold);
    for(unsigned tick = 0U; tick < 100U; tick++)
        files_controller_tick(&state, HK_INPUT_BUTTON_OK);
    check(
        g_delete_confirm == 0U,
        "missing timing must never trigger destructive hold action");

    if(g_failures)
        return 1;
    puts("FILES_CONTROLLER_OK open=1 monotonic=1 failure_safe=1");
    return 0;
}
