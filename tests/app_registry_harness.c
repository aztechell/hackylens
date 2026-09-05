#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hk_config.h"
#include "firmware/generated/app_registry/registry.h"
#include "firmware/src/core/hk_app_registry.h"
#include <hackylens/app.h>

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if(!(expression))                                                     \
        {                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
                    #expression);                                             \
            exit(1);                                                          \
        }                                                                     \
    } while(0)

static uint32_t s_background_calls;
static uint32_t s_sd_calls;
static uint32_t s_debug_calls;

static void noop_enter(const hk_input_snapshot_t *input) { (void)input; }
static void count_background(const hk_input_snapshot_t *input)
{
    (void)input;
    s_background_calls++;
}
static void count_sd(hk_sd_event_t event)
{
    (void)event;
    s_sd_calls++;
}
static uint8_t count_debug(const char *command)
{
    s_debug_calls++;
    return command && strcmp(command, "APPDEBUG") == 0 ? 1U : 0U;
}
static uint8_t owns_camera_settings(screen_t screen)
{
    return screen == SCREEN_CAMERA_SETTINGS ? 1U : 0U;
}

static void dummy_draw_icon(
    uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    (void)x;
    (void)y;
    (void)color;
    (void)bg;
}

void apriltag_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}
void buttons_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}
void camera_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}
void face_detect_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}
void files_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}
void micropython_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}
void object_detect_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}
void pong_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}
void qr_camera_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}
void settings_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}
void sleep_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}
void terminal_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    dummy_draw_icon(x, y, color, bg);
}

const hk_legacy_app_entry_t apriltag_legacy_entry = {
    .screen = SCREEN_APRILTAG,
    .enter = noop_enter,
};
static uint8_t s_buttons_v2_storage[16];
static hk_result_t dummy_buttons_start(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
static hk_result_t dummy_buttons_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    (void)ctx;
    (void)event;
    return HK_OK;
}
static hk_result_t dummy_buttons_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    (void)ctx;
    (void)surface;
    return HK_OK;
}
static hk_result_t dummy_buttons_stop(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
const hk_app_v2_entry_t buttons_v2_entry = {
    .state_storage = s_buttons_v2_storage,
    .state_capacity_bytes = sizeof(s_buttons_v2_storage),
    .start = dummy_buttons_start,
    .event = dummy_buttons_event,
    .render = dummy_buttons_render,
    .stop = dummy_buttons_stop,
};
const hk_legacy_app_entry_t camera_legacy_entry = {
    .screen = SCREEN_CAMERA,
    .enter = noop_enter,
    .owns_screen = owns_camera_settings,
    .blocks_sd_poll = 1U,
    .handle_debug_command = count_debug,
};
const hk_legacy_app_entry_t face_detect_legacy_entry = {
    .screen = SCREEN_FACE_DETECT,
    .enter = noop_enter,
    .background_tick = count_background,
};
const hk_legacy_app_entry_t object_detect_legacy_entry = {
    .screen = SCREEN_OBJECT_DETECT,
    .enter = noop_enter,
    .handle_sd_event = count_sd,
};
static uint8_t s_files_v2_storage[16];
static hk_result_t dummy_files_start(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
static hk_result_t dummy_files_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    (void)ctx;
    (void)event;
    return HK_OK;
}
static hk_result_t dummy_files_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    (void)ctx;
    (void)surface;
    return HK_OK;
}
static hk_result_t dummy_files_stop(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
const hk_app_v2_entry_t files_v2_entry = {
    .state_storage = s_files_v2_storage,
    .state_capacity_bytes = sizeof(s_files_v2_storage),
    .start = dummy_files_start,
    .event = dummy_files_event,
    .render = dummy_files_render,
    .stop = dummy_files_stop,
};
const hk_legacy_app_entry_t micropython_legacy_entry = {
    .screen = SCREEN_APP_SLOT_2,
    .enter = noop_enter,
};
static uint8_t s_pong_v2_storage[16];
static hk_result_t dummy_pong_start(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
static hk_result_t dummy_pong_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    (void)ctx;
    (void)event;
    return HK_OK;
}
static hk_result_t dummy_pong_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    (void)ctx;
    (void)surface;
    return HK_OK;
}
static hk_result_t dummy_pong_stop(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
const hk_app_v2_entry_t pong_v2_entry = {
    .state_storage = s_pong_v2_storage,
    .state_capacity_bytes = sizeof(s_pong_v2_storage),
    .start = dummy_pong_start,
    .event = dummy_pong_event,
    .render = dummy_pong_render,
    .stop = dummy_pong_stop,
};
static uint8_t s_qr_camera_v2_storage[16];
static hk_result_t dummy_qr_camera_start(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
static hk_result_t dummy_qr_camera_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    (void)ctx;
    (void)event;
    return HK_OK;
}
static hk_result_t dummy_qr_camera_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    (void)ctx;
    (void)surface;
    return HK_OK;
}
static hk_result_t dummy_qr_camera_stop(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
const hk_app_v2_entry_t qr_camera_v2_entry = {
    .state_storage = s_qr_camera_v2_storage,
    .state_capacity_bytes = sizeof(s_qr_camera_v2_storage),
    .start = dummy_qr_camera_start,
    .event = dummy_qr_camera_event,
    .render = dummy_qr_camera_render,
    .stop = dummy_qr_camera_stop,
};
static uint8_t s_settings_v2_storage[16];
static hk_result_t dummy_settings_start(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
static hk_result_t dummy_settings_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    (void)ctx;
    (void)event;
    return HK_OK;
}
static hk_result_t dummy_settings_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    (void)ctx;
    (void)surface;
    return HK_OK;
}
static hk_result_t dummy_settings_stop(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
const hk_app_v2_entry_t settings_v2_entry = {
    .state_storage = s_settings_v2_storage,
    .state_capacity_bytes = sizeof(s_settings_v2_storage),
    .start = dummy_settings_start,
    .event = dummy_settings_event,
    .render = dummy_settings_render,
    .stop = dummy_settings_stop,
};
static uint8_t s_sleep_v2_storage[16];
static hk_result_t dummy_sleep_start(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
static hk_result_t dummy_sleep_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    (void)ctx;
    (void)event;
    return HK_OK;
}
static hk_result_t dummy_sleep_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    (void)ctx;
    (void)surface;
    return HK_OK;
}
static hk_result_t dummy_sleep_stop(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
const hk_app_v2_entry_t sleep_v2_entry = {
    .state_storage = s_sleep_v2_storage,
    .state_capacity_bytes = sizeof(s_sleep_v2_storage),
    .start = dummy_sleep_start,
    .event = dummy_sleep_event,
    .render = dummy_sleep_render,
    .stop = dummy_sleep_stop,
};
static uint8_t s_terminal_v2_storage[16];
static hk_result_t dummy_terminal_start(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
static hk_result_t dummy_terminal_event(
    const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    (void)ctx;
    (void)event;
    return HK_OK;
}
static hk_result_t dummy_terminal_render(
    const hk_app_context_t *ctx, hk_app_surface_t *surface)
{
    (void)ctx;
    (void)surface;
    return HK_OK;
}
static hk_result_t dummy_terminal_stop(const hk_app_context_t *ctx)
{
    (void)ctx;
    return HK_OK;
}
const hk_app_v2_entry_t terminal_v2_entry = {
    .state_storage = s_terminal_v2_storage,
    .state_capacity_bytes = sizeof(s_terminal_v2_storage),
    .start = dummy_terminal_start,
    .event = dummy_terminal_event,
    .render = dummy_terminal_render,
    .stop = dummy_terminal_stop,
};

static const hk_app_t *app_by_id(const char *id)
{
    for(uint8_t index = 0U; index < g_hk_generated_app_count; index++)
    {
        if(strcmp(g_hk_generated_apps[index]->id, id) == 0)
            return g_hk_generated_apps[index];
    }
    return NULL;
}

int main(void)
{
    const hk_app_t *camera;
    const hk_app_t *settings;
    const hk_app_t *sleep;
    const hk_input_snapshot_t input = {0U, 0U, 0U};

#if HK_ENABLE_APP_MICROPYTHON
    CHECK(g_hk_generated_app_count == 12U);
    CHECK(g_menu_item_count == 12U);
    CHECK(strcmp(g_menu_items[11]->id, "micropython") == 0);
    CHECK(hk_app_for_autostart_id(10U) == app_by_id("micropython"));
#else
    CHECK(g_hk_generated_app_count == 11U);
    CHECK(g_menu_item_count == 11U);
    CHECK(strcmp(g_menu_items[10]->id, "sleep") == 0);
    CHECK(hk_app_for_autostart_id(10U) == NULL);
#endif
    CHECK(hk_app_autostart_id_is_persistable(HK_AUTOSTART_OFF) == 1U);
    CHECK(hk_app_autostart_id_is_persistable(10U) == 1U);
    CHECK(hk_app_autostart_id_is_persistable(11U) == 0U);
    CHECK(hk_app_autostart_id_is_persistable(65535U) == 0U);
    CHECK(strcmp(g_hk_generated_apps[0]->id, "apriltag") == 0);
    CHECK(strcmp(g_hk_generated_apps[g_hk_generated_app_count - 1U]->id,
                 "terminal") == 0);
    CHECK(strcmp(g_menu_items[0]->id, "terminal") == 0);
    CHECK(strcmp(hk_app_for_autostart_id(6U)->id, "files") == 0);
    CHECK(strcmp(hk_app_for_autostart_id(9U)->id, "object-detect") == 0);
    CHECK(strcmp(hk_app_autostart_at(5U)->id,
#if HK_ENABLE_APP_MICROPYTHON
                 "micropython"
#else
                 "object-detect"
#endif
                 ) == 0);
    CHECK(hk_app_autostart_count() ==
#if HK_ENABLE_APP_MICROPYTHON
          10U
#else
          9U
#endif
    );

    camera = app_by_id("camera");
    settings = app_by_id("settings");
    sleep = app_by_id("sleep");
    CHECK(camera != NULL && settings != NULL && sleep != NULL);
    CHECK(hk_app_for_screen(SCREEN_CAMERA) == camera);
    CHECK(hk_app_for_screen(SCREEN_CAMERA_SETTINGS) == camera);
    CHECK(hk_app_registry_sd_poll_allowed(SCREEN_CAMERA) == 0U);
    CHECK(hk_app_registry_sd_poll_allowed(SCREEN_BUTTONS) == 1U);
    CHECK(camera->limits.tick_interval_us == 1000U);
    CHECK(app_by_id("buttons")->limits.tick_interval_us == 20000U);
    CHECK(camera->service_count == 2U);
    CHECK(strcmp(camera->services[0].id,
                 "hackylens.service.legacy-camera") == 0);
    CHECK(settings->capability_count == 2U);
    CHECK(sleep->capability_count == 3U);
    CHECK(app_by_id("files")->capability_count == 3U);
    CHECK(app_by_id("files")->service_count == 0U);
    CHECK(app_by_id("files")->limits.tick_interval_us == 200000U);
    CHECK(app_by_id("qr-camera")->capability_count == 3U);
    CHECK(app_by_id("qr-camera")->service_count == 0U);
    CHECK(app_by_id("qr-camera")->limits.tick_interval_us == 200000U);
    for(uint16_t index = 0U; index < settings->capability_count; index++)
    {
        const hk_app_capability_request_t *request =
            &settings->capabilities[index];
        CHECK(request->optional == 0U);
        CHECK(strcmp(request->id, "hackylens.cap.lights") != 0);
        CHECK(strcmp(request->id, "hackylens.cap.external-link") != 0);
    }
    for(uint16_t index = 0U; index < sleep->capability_count; index++)
        CHECK(strcmp(sleep->capabilities[index].id,
                     "hackylens.cap.lights") != 0);

    hk_app_registry_background_tick(&input);
    CHECK(s_background_calls == 1U);
    hk_app_registry_handle_sd_event(HK_SD_EVENT_INSERTED);
    CHECK(s_sd_calls == 1U);
    CHECK(hk_app_registry_handle_debug_command("APPDEBUG") == 1U);
    CHECK(s_debug_calls == 1U);

    printf("APP_REGISTRY_OK apps=%u menu=%u\n",
           g_hk_generated_app_count, g_menu_item_count);
    return 0;
}
