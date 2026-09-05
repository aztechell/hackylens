#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "app_runtime_host_support.h"
#include "../firmware/src/apps/files/files_app.h"
#include "../firmware/src/apps/files/file_browser_mode.h"
#include "../firmware/src/core/hk_events.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("FAIL line=%d: %s\n", __LINE__, #condition);             \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static unsigned s_enter_calls;
static unsigned s_exit_calls;

void files_view_init(void) {}
void files_backend_enter(void) { s_enter_calls++; }
void files_refresh_after_sd_event(hk_sd_event_t event) { (void)event; }
void files_nav_delta(int8_t delta) { (void)delta; }
uint8_t files_back_from_list(void) { return 1U; }
void files_open_selected(void) {}
uint8_t files_delete_confirm_enter(void) { return 0U; }
void files_delete_cancel(void) {}
void files_delete_confirmed(void) {}
void files_presenter_close_image(void) { s_exit_calls++; }
void files_presenter_render_list(void) {}
void files_presenter_tick_image(uint64_t now_us) { (void)now_us; }
uint8_t files_presenter_toggle_image_pause(uint64_t now_us)
{
    (void)now_us;
    return 0U;
}
file_browser_mode_t files_mode(void) { return FILES_MODE_LIST; }
void files_set_mode(file_browser_mode_t mode) { (void)mode; }

static int dispatch_input(
    hk_app_runtime_host_t *host, uint32_t state, uint8_t *consumed)
{
    hk_input_event_t input = {0};

    CHECK(hk_app_runtime_host_push_input(host, state) == HK_OK);
    input.sequence = 1U;
    input.timestamp_us = hk_app_runtime_host_now_us(host);
    input.state = state;
    input.changed = state;
    input.pressed = state;
    CHECK(hk_app_switch_input(
              hk_app_runtime_host_switch(host), &input, consumed) == HK_OK);
    return 0;
}

int main(void)
{
    hk_app_runtime_host_t host;
    hk_app_t app;
    hk_app_switch_t *switcher;
    uint8_t consumed = 0U;

    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    hk_app_runtime_host_fill_app(
        &app, "files", &files_v2_entry, 1024U);
    switcher = hk_app_runtime_host_switch(&host);
    CHECK(hk_app_switch_open(switcher, &app, NULL) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == &app);
    CHECK(s_enter_calls == 1U);

    CHECK(dispatch_input(&host, HK_INPUT_BUTTON_BACK, &consumed) == 0);
    CHECK(consumed == 1U);
    CHECK(hk_app_switch_active(switcher) == NULL);
    CHECK(s_exit_calls >= 1U);

    CHECK(hk_app_runtime_host_init(&host) == HK_OK);
    s_enter_calls = 0U;
    s_exit_calls = 0U;
    hk_app_runtime_host_fill_app(
        &app, "files", &files_v2_entry, 1024U);
    switcher = hk_app_runtime_host_switch(&host);
    CHECK(hk_app_switch_open(switcher, &app, NULL) == HK_OK);
    CHECK(hk_app_switch_close(switcher, HK_APP_STOP_COMPLETED) == HK_OK);
    CHECK(hk_app_switch_active(switcher) == NULL);
    CHECK(hk_app_runtime_host_owner_cleanup_calls(&host) >= 1U);

    printf("FILES_RUNTIME_OK\n");
    return 0;
}
