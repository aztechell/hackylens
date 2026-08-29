#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hk_config.h"
#include "../firmware/generated/app_registry/registry.h"
#include "../firmware/src/core/hk_app_registry.h"

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

const hk_legacy_app_entry_t apriltag_legacy_entry = {
    .screen = SCREEN_APRILTAG,
    .enter = noop_enter,
};
const hk_legacy_app_entry_t buttons_legacy_entry = {
    .screen = SCREEN_BUTTONS,
    .enter = noop_enter,
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
const hk_legacy_app_entry_t files_legacy_entry = {
    .screen = SCREEN_FILES,
    .enter = noop_enter,
    .handle_sd_event = count_sd,
};
const hk_legacy_app_entry_t micropython_legacy_entry = {
    .screen = SCREEN_APP_SLOT_2,
    .enter = noop_enter,
};
const hk_legacy_app_entry_t object_detect_legacy_entry = {
    .screen = SCREEN_OBJECT_DETECT,
    .enter = noop_enter,
};
const hk_legacy_app_entry_t pong_legacy_entry = {
    .screen = SCREEN_APP_SLOT_0,
    .enter = noop_enter,
};
const hk_legacy_app_entry_t qr_camera_legacy_entry = {
    .screen = SCREEN_QR_CAMERA,
    .enter = noop_enter,
};
const hk_legacy_app_entry_t settings_legacy_entry = {
    .screen = SCREEN_SETTINGS,
    .enter = noop_enter,
};
const hk_legacy_app_entry_t sleep_legacy_entry = {
    .screen = SCREEN_SLEEP,
    .enter = noop_enter,
};
const hk_legacy_app_entry_t terminal_legacy_entry = {
    .screen = SCREEN_APP_SLOT_1,
    .enter = noop_enter,
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
    const hk_input_snapshot_t input = {0U, 0U, 0U};
    uint8_t optional_found = 0U;

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
    CHECK(camera != NULL && settings != NULL);
    CHECK(hk_app_for_screen(SCREEN_CAMERA) == camera);
    CHECK(hk_app_for_screen(SCREEN_CAMERA_SETTINGS) == camera);
    CHECK(hk_app_registry_sd_poll_allowed(SCREEN_CAMERA) == 0U);
    CHECK(hk_app_registry_sd_poll_allowed(SCREEN_BUTTONS) == 1U);
    CHECK(camera->limits.tick_interval_us == 1000U);
    CHECK(app_by_id("buttons")->limits.tick_interval_us == 20000U);
    CHECK(camera->service_count == 2U);
    CHECK(strcmp(camera->services[0].id,
                 "hackylens.service.legacy-camera") == 0);
    for(uint16_t index = 0U; index < settings->capability_count; index++)
    {
        const hk_app_capability_request_t *request =
            &settings->capabilities[index];
        if(request->optional && request->fallback &&
           strcmp(request->fallback, "hide-external-link-menu") == 0)
            optional_found = 1U;
    }
    CHECK(optional_found == 1U);

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
