#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/src/config/settings_config.h"
#include "../firmware/src/config/input_config.h"
#include "../firmware/src/apps/apriltag/apriltag_settings.h"
#include "../firmware/src/apps/apriltag/apriltag_settings_menu.h"
#include "../firmware/src/apps/camera/camera_settings_menu.h"
#include "../firmware/src/apps/object_detect/object_detect_labels.h"
#include "../firmware/src/apps/object_detect/object_detect_settings.h"
#include "../firmware/src/apps/object_detect/object_detect_settings_menu.h"
#include "../firmware/src/apps/qr_camera/qr_config.h"
#include "../firmware/src/apps/qr_camera/qr_settings_menu.h"
#include "../firmware/src/core/hk_camera_sizes.h"
#include "../firmware/src/services/camera_light.h"
#include "../firmware/src/services/camera_persist_settings.h"
#include "../firmware/src/services/camera_session_preferences.h"
#include "../firmware/src/services/object_detect_postprocess.h"
#include "../firmware/src/apps/settings/settings_menu.h"
#include "../firmware/src/apps/settings/settings_controller.h"
#include "../firmware/src/core/hk_ui_colors.h"
#include "../firmware/src/core/hk_menu_runtime.h"
#include "../firmware/src/core/hk_app_registry.h"
#include "../firmware/src/core/hk_binary.h"
#include "../firmware/src/services/settings_payload_codec.h"
#include "../firmware/src/services/settings_persistence.h"
#include "../firmware/src/services/settings_app_data.h"
#include "../firmware/src/services/settings_service.h"
#include "../firmware/src/storage/internal_flash.h"
#include "../firmware/src/storage/settings_store.h"
#include "../firmware/src/storage/fat32_volume.h"
#include "../firmware/src/ui/display_binding.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            fprintf(stderr, "SETTINGS_AUTOSTART_FAIL line=%d\n", __LINE__); \
            return 1;                                                        \
        }                                                                    \
    } while(0)

typedef struct
{
    uint8_t led_enabled;
    uint8_t led_brightness;
    uint8_t rgb_enabled;
    uint8_t rgb_brightness;
    uint8_t screen_brightness;
    uint8_t camera_review_after_shot;
    uint8_t auto_sleep_minutes;
    uint8_t camera_format_rgb_red;
    uint8_t camera_size_rgb_green;
    uint8_t camera_schema_mark;
    uint8_t rgb_red_light_mode;
    uint8_t rgb_green;
    uint8_t rgb_blue;
    uint8_t rgb_schema_mark;
    uint8_t fps_rgb_blue;
    uint8_t qr_rate_fps_mark;
    uint8_t app_data[SETTINGS_APP_DATA_SIZE];
    uint8_t autostart_id;
} fixture_payload_v4_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t sequence;
    fixture_payload_v4_t payload;
    uint32_t crc32;
} fixture_record_v4_t;

static uint8_t s_flash[2][SETTINGS_FLASH_SLOT_SIZE];
static fat32_usage_t s_usage;
static const hk_app_t s_camera = {.id = "camera", .title = "CAMERA"};
static const hk_app_t s_qr = {.id = "qr-camera", .title = "QR-CAMERA"};
static const hk_app_t s_files = {.id = "files", .title = "FILES"};
static const hk_app_t s_terminal = {.id = "terminal", .title = "TERMINAL"};
static const hk_app_t s_settings = {.id = "settings", .title = "SETTINGS"};
static const hk_app_t s_power = {.id = "power", .title = "POWER"};
const hk_app_t *const g_menu_items[] = {
    &s_camera, &s_qr, &s_files, &s_terminal, &s_settings, &s_power,
};
const uint8_t g_menu_item_count = sizeof(g_menu_items) / sizeof(g_menu_items[0]);
static screen_t s_screen;
static const char *s_pin;
static const char *s_drawn_first;
static const hk_app_t *s_opened_app;
static const char *s_opened_pin;
static uint8_t s_has_accelerometer;

void hk_back_exit_set_armed(uint8_t armed) { (void)armed; }
void hk_screen_set(screen_t screen) { s_screen = screen; }
screen_t hk_screen_get(void) { return s_screen; }
const hk_app_t *hk_app_for_id(const char *id)
{
    for(uint8_t i = 0; i < g_menu_item_count; i++)
        if(strcmp(id, g_menu_items[i]->id) == 0)
            return g_menu_items[i];
    return NULL;
}
static uint8_t menu_pin_count(void) { return s_pin != NULL; }
static const char *menu_pin_name(uint8_t index) { return index == 0U ? s_pin : NULL; }
static uint8_t menu_open_pin(const char *name, const hk_input_snapshot_t *input)
{
    (void)input;
    s_opened_pin = name;
    return 1U;
}
static uint8_t menu_open_app(const hk_app_t *app, const hk_input_snapshot_t *input)
{
    (void)input;
    s_opened_app = app;
    return 1U;
}
static void menu_draw_app(uint8_t slot, const hk_app_t *app, uint8_t selected)
{
    (void)selected;
    if(slot == 0U)
        s_drawn_first = app->title;
}
static void menu_draw_pin(uint8_t slot, const char *name, uint8_t selected)
{
    (void)selected;
    if(slot == 0U)
        s_drawn_first = name;
}

uint8_t hk_app_autostart_count(void) { return 0U; }
const hk_app_t *hk_app_autostart_at(uint8_t index) { (void)index; return NULL; }
uint8_t hk_sd_present(void) { return s_usage.status != FAT32_USAGE_UNAVAILABLE; }
uint8_t hk_fat_mounted(void) { return hk_sd_present(); }
fat32_usage_t fat32_usage(void) { return s_usage; }
uint8_t audio_service_available(void) { return 0U; }
hk_result_t audio_service_set_volume(uint8_t percent) { (void)percent; return HK_OK; }
void external_link_service_set_transport(external_link_transport_t transport) { (void)transport; }
void external_link_service_set_uart_baud(uint32_t baud) { (void)baud; }
void activity_note(void) {}
void rgb_led_apply(void) {}
void illum_led_apply(void) {}
void screen_brightness_apply(void) {}
void settings_backlight_preview(uint8_t percent) { (void)percent; }
void settings_illumination_preview(uint8_t percent) { (void)percent; }
void settings_rgb_preview(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
}
void camera_session_preferences_override(const camera_session_preferences_t *preferences)
{
    (void)preferences;
}
void apriltag_detector_set_refine_edges(uint8_t enabled) { (void)enabled; }
void camera_light_apply(void) {}
const char *camera_light_mode_label(camera_light_mode_t mode)
{
    return mode == CAMERA_LIGHT_RGB ? "RGB" : "LED";
}
const char *camera_log_prefix(void) { return "[CAM]"; }
void camera_active_size_set(camera_size_t size) { (void)size; }
uint64_t hal_time_us(void) { return 0U; }
uint8_t camera_session_preferences_fps_enabled(void) { return camera_service_fps_enabled(); }
void camera_service_frame_info(uint16_t *width, uint16_t *height)
{
    *width = camera_size_width(camera_service_size());
    *height = camera_size_height(camera_service_size());
}
uint8_t qr_service_decode_rate(void) { return settings_qr_decode_rate(); }
uint8_t qr_service_cycle_decode_rate(void)
{
    uint8_t rate = settings_qr_decode_rate();
    settings_set_qr_decode_rate(rate >= QR_DECODE_RATE_MAX ? QR_DECODE_RATE_MIN : rate + 1U);
    return settings_qr_decode_rate();
}

void object_detect_detector_set_thresholds(uint8_t confidence, uint8_t nms)
{
    (void)confidence;
    (void)nms;
}
uint8_t apriltag_controller_arm_calibration(void) { return 0U; }
uint8_t apriltag_controller_calibration_armed(void) { return 0U; }
void apriltag_controller_cancel_calibration(void) {}
uint8_t apriltag_controller_has_accelerometer(void) { return s_has_accelerometer; }
uint8_t apriltag_controller_capture_gravity(uint8_t lens_down) { (void)lens_down; return 0U; }
void menu_draw_chrome(const char *title)
{
    unsigned margin = HK_DISPLAY_REQUIRED_WIDTH >= 320U ? 104U : 46U;
    assert(strlen(title) * HACKYLENS_FONT_W <= HK_DISPLAY_REQUIRED_WIDTH - margin);
}
void hk_ui_display_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                             uint16_t color)
{
    (void)color;
    assert(x + width <= HK_DISPLAY_REQUIRED_WIDTH && y + height <= HK_DISPLAY_REQUIRED_HEIGHT);
}
void hk_ui_display_draw_text_clipped_at(uint16_t x, uint16_t y, uint16_t width,
                                        const char *text, uint16_t foreground,
                                        uint16_t background)
{
    (void)foreground;
    (void)background;
    assert(x + width <= HK_DISPLAY_REQUIRED_WIDTH);
    assert((unsigned)y + HACKYLENS_FONT_H <= HK_DISPLAY_REQUIRED_HEIGHT);
    assert(strlen(text) * HACKYLENS_FONT_W <= width);
}
void hk_ui_display_draw_text_at(uint16_t x, uint16_t y, const char *text,
                                uint16_t foreground, uint16_t background)
{
    (void)foreground;
    (void)background;
    assert(x + strlen(text) * HACKYLENS_FONT_W <= HK_DISPLAY_REQUIRED_WIDTH);
    assert((unsigned)y + HACKYLENS_FONT_H <= HK_DISPLAY_REQUIRED_HEIGHT);
}

static settings_menu_event_t press(settings_menu_session_t *menu, uint32_t button)
{
    const hk_input_snapshot_t input = {button, button, button};
    return settings_menu_handle_input(menu, &input);
}

static void settings_press(settings_state_t *state, uint32_t button)
{
    const hk_input_event_t event = {.state = button, .pressed = button, .changed = button};
    settings_controller_handle_input(state, &event);
}

static uint8_t menu_write_order(const hk_menu_order_t *order)
{
    if(!settings_set_menu_order(order))
        return 0U;
    settings_mark_dirty(1U);
    return 1U;
}

static int test_menu_order(void)
{
    const hk_menu_owner_hooks_t owner = {.enter = menu_open_app,
        .shortcut_count = menu_pin_count, .shortcut_name = menu_pin_name,
        .open_shortcut = menu_open_pin, .read_order = settings_menu_order,
        .write_order = menu_write_order};
    const hk_menu_view_t view = {.draw_item_at = menu_draw_app, .draw_shortcut_at = menu_draw_pin};
    settings_state_t state;
    hk_menu_item_t items[HK_MENU_ORDER_CAPACITY];
    uint8_t count = g_menu_item_count + 1U;

    memset(s_flash, 0xFF, sizeof(s_flash));
    settings_storage_init();
    s_pin = "tracker.py";
    menu_owner_hooks_set(&owner);
    menu_view_set(&view);
    menu_render();
    CHECK(strcmp(s_drawn_first, "CAMERA") == 0);
    CHECK(shell_open_app(&s_settings, NULL));
    settings_controller_reset(&state, settings_app_menu_definition());
    settings_press(&state, BUTTON_LEFT);
    settings_press(&state, BUTTON_OK);
    CHECK(state.page == SETTINGS_PAGE_MENU_ORDER && state.order.count == count);

    settings_press(&state, BUTTON_LEFT);
    CHECK(state.order.index == count - 1U && state.order.top > 0U);
    settings_press(&state, BUTTON_OK);
    for(uint8_t i = 0; i < count; i++)
        settings_press(&state, BUTTON_LEFT);
    CHECK(state.order.index == 0U && state.order.top == 0U);
    CHECK(hk_menu_get_items(items) == count && items[0].app == &s_camera);
    settings_press(&state, BUTTON_OK);
    CHECK(hk_menu_get_items(items) == count && strcmp(items[0].shortcut, s_pin) == 0);

    settings_press(&state, BUTTON_RIGHT);
    settings_press(&state, BUTTON_OK);
    settings_press(&state, BUTTON_RIGHT);
    settings_press(&state, BUTTON_BACK);
    CHECK(state.order.index == 1U && state.order.items[1].app == &s_camera);
    CHECK(hk_menu_get_items(items) == count && items[1].app == &s_camera);
    settings_press(&state, BUTTON_OK);
    settings_press(&state, BUTTON_RIGHT);
    settings_press(&state, BUTTON_OK);
    settings_press(&state, BUTTON_BACK);
    CHECK(state.page == SETTINGS_PAGE_ROOT && !state.close_requested);
    settings_controller_exit(&state);
    shell_show_menu();
    CHECK(strcmp(s_drawn_first, s_pin) == 0 && hk_menu_index_get() == 5U);
    for(uint8_t i = 0; i < 5U; i++)
        menu_select_delta(-1);
    shell_open_selected(NULL);
    CHECK(s_opened_pin && strcmp(s_opened_pin, s_pin) == 0);
    menu_select_delta(1);
    shell_open_selected(NULL);
    CHECK(s_opened_app == &s_qr);

    settings_storage_tick();
    settings_storage_tick();
    settings_storage_init();
    CHECK(hk_menu_get_items(items) == count && items[2].app == &s_camera);
    CHECK(items[1].app == &s_qr && strcmp(items[0].shortcut, s_pin) == 0);
    s_pin = NULL;
    CHECK(hk_menu_get_items(items) == count - 1U && items[0].app == &s_qr);
    s_pin = "new.py";
    CHECK(hk_menu_get_items(items) == count && items[0].app == &s_qr);
    CHECK(items[count - 1U].shortcut && strcmp(items[count - 1U].shortcut, s_pin) == 0);
    return 0;
}

static int test_object_classes(void)
{
    settings_menu_session_t menu = {0};
    const char *title;
    char value[24];
    uint32_t selected = (UINT32_C(1) << 6U) | (UINT32_C(1) << 14U);
    static float output[OBJECT_DETECT_OUTPUT_BYTES / sizeof(float)];
    static object_detect_postprocess_workspace_t workspace;
    object_detect_result_t results[OBJECT_DETECT_RESULT_MAX];

    memset(s_flash, 0xFF, sizeof(s_flash));
    settings_storage_init();
    CHECK(settings_menu_open(&menu, object_detect_settings_menu_definition()));
    CHECK(settings_menu_visible_slot(&menu, 0U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Detection") == 0 && strcmp(value, ">") == 0);
    CHECK(press(&menu, BUTTON_OK) == SETTINGS_MENU_EVENT_NONE);
    CHECK(settings_menu_visible_slot(&menu, 0U, &title, NULL, 0U, NULL, NULL));
    CHECK(strcmp(title, "Confidence") == 0);
    CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_NONE);
    press(&menu, BUTTON_RIGHT);
    CHECK(settings_menu_visible_slot(&menu, 1U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Classes") == 0 && strcmp(value, "20/20 >") == 0);
    CHECK(press(&menu, BUTTON_OK) == SETTINGS_MENU_EVENT_NONE);
    CHECK(strcmp(menu.definition->title, "OBJ CLASSES") == 0);
    CHECK(menu.definition->item_count == OBJECT_DETECT_CLASS_COUNT + 2U);
    for(uint8_t class_id = 0; class_id < OBJECT_DETECT_CLASS_COUNT; class_id++)
    {
        const settings_menu_item_t *item = &menu.definition->items[class_id + 2U];
        CHECK(item->id == class_id && item->kind == SETTINGS_MENU_ITEM_TOGGLE);
        CHECK(strcmp(item->title, object_detect_label(class_id)) == 0);
        CHECK(settings_menu_display_value(&menu, class_id) == 1);
    }

    press(&menu, BUTTON_RIGHT);
    CHECK(press(&menu, BUTTON_OK) == SETTINGS_MENU_EVENT_NONE);
    CHECK(object_detect_settings_class_mask() == 0U);
    for(unsigned tick = 0; tick <= SETTINGS_SAVE_DELAY_TICKS; tick++)
        settings_storage_tick();
    settings_storage_init();
    CHECK(object_detect_settings_class_mask() == 0U);

    for(unsigned step = 0; step < 7U; step++)
        press(&menu, BUTTON_RIGHT);
    CHECK(menu.definition->items[menu.index].id == 6U);
    press(&menu, BUTTON_OK);
    for(unsigned step = 0; step < 8U; step++)
        press(&menu, BUTTON_RIGHT);
    CHECK(menu.definition->items[menu.index].id == 14U);
    press(&menu, BUTTON_OK);
    CHECK(object_detect_settings_class_mask() == selected);
    CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_NONE);
    CHECK(menu.index == 1U && menu.top == 0U);
    CHECK(settings_menu_visible_slot(&menu, 1U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Classes") == 0 && strcmp(value, "2/20 >") == 0);
    CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_CLOSE_REQUESTED);
    settings_menu_close(&menu);
    CHECK(settings_storage_flush());
    settings_storage_init();
    CHECK(object_detect_settings_class_mask() == selected);

    /* Nine strong cats fill the unfiltered output ahead of the car and person. */
    const unsigned grid = OBJECT_DETECT_GRID_CELLS;
    for(unsigned anchor = 0; anchor < OBJECT_DETECT_ANCHOR_COUNT; anchor++)
    {
        unsigned base = anchor * (OBJECT_DETECT_CLASS_COUNT + 5U) * grid;
        for(unsigned cell = 0; cell < grid; cell++)
            output[base + 4U * grid + cell] = -12.0f;
    }
    for(unsigned cell = 0; cell < 11U; cell++)
    {
        unsigned class_id = cell < 9U ? 7U : (cell == 9U ? 6U : 14U);
        output[2U * grid + cell] = -1.0f;
        output[3U * grid + cell] = -1.0f;
        output[4U * grid + cell] = cell < 9U ? 8.0f : 5.0f;
        output[(5U + class_id) * grid + cell] = 8.0f;
    }
    CHECK(object_detect_postprocess(output, sizeof(output), 50U, 20U,
          OBJECT_DETECT_ALL_CLASSES, &workspace, results, NULL, NULL) == OBJECT_DETECT_RESULT_MAX);
    for(uint8_t index = 0; index < OBJECT_DETECT_RESULT_MAX; index++)
        CHECK(results[index].class_id == 7U);
    CHECK(object_detect_postprocess(output, sizeof(output), 50U, 20U,
          object_detect_settings_class_mask(), &workspace, results, NULL, NULL) == 2U);
    CHECK(results[0].class_id == 6U && results[1].class_id == 14U);
    CHECK(object_detect_postprocess(output, sizeof(output), 50U, 20U,
          0U, &workspace, results, NULL, NULL) == 0U);

    CHECK(settings_menu_open(&menu, object_detect_settings_menu_definition()));
    press(&menu, BUTTON_RIGHT);
    press(&menu, BUTTON_OK);
    press(&menu, BUTTON_OK);
    CHECK(object_detect_settings_class_mask() == OBJECT_DETECT_ALL_CLASSES);
    CHECK(settings_storage_flush());
    settings_storage_init();
    CHECK(object_detect_settings_class_mask() == OBJECT_DETECT_ALL_CLASSES);
    return 0;
}

static int test_screen_and_device(void)
{
    settings_menu_session_t menu = {0};
    const char *title;
    char value[24];
    CHECK(settings_menu_bind(&menu, settings_app_page_definition(SETTINGS_PAGE_SCREEN)));
    press(&menu, HK_INPUT_BUTTON_RIGHT);
    CHECK(settings_menu_visible_slot(&menu, 1U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "UI Color") == 0 && strcmp(value, "Green") == 0);
    press(&menu, HK_INPUT_BUTTON_OK);
    press(&menu, HK_INPUT_BUTTON_RIGHT);
    CHECK(settings_ui_color() == HK_UI_COLOR_GREEN && hk_ui_accent() == 0x07ffU);
    CHECK(settings_menu_visible_slot(&menu, 1U, NULL, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(value, "Cyan") == 0);
    press(&menu, HK_INPUT_BUTTON_BACK);
    CHECK(settings_ui_color() == HK_UI_COLOR_GREEN && hk_ui_accent() == 0x07e0U);
    press(&menu, HK_INPUT_BUTTON_OK);
    press(&menu, HK_INPUT_BUTTON_LEFT);
    CHECK(settings_ui_color() == HK_UI_COLOR_GREEN && hk_ui_accent() == 0xffffU);
    CHECK(settings_menu_visible_slot(&menu, 1U, NULL, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(value, "White") == 0);
    press(&menu, HK_INPUT_BUTTON_OK);
    CHECK(settings_ui_color() == HK_UI_COLOR_WHITE);
    settings_storage_init();
    CHECK(settings_ui_color() == HK_UI_COLOR_WHITE && hk_ui_accent() == 0xffffU);

    CHECK(settings_menu_bind(&menu, settings_app_page_definition(SETTINGS_PAGE_DEVICE)));
    CHECK(settings_menu_visible_slot(&menu, 3U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Used") == 0 && strcmp(value, "-") == 0);
    s_usage = (fat32_usage_t){.status = FAT32_USAGE_SCANNING, .progress = 36U,
                              .total_bytes = UINT64_C(64) << 30};
    CHECK(settings_menu_visible_slot(&menu, 3U, NULL, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(value, "SCAN 30%") == 0);
    CHECK(settings_menu_visible_slot(&menu, 4U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Total") == 0 && strcmp(value, "64.0 GiB") == 0);
    s_usage.status = FAT32_USAGE_READY;
    s_usage.used_bytes = UINT64_C(17) << 29;
    CHECK(settings_menu_visible_slot(&menu, 3U, NULL, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(value, "8.5 GiB") == 0);
    return 0;
}

static int test_camera_and_qr_groups(void)
{
    settings_menu_session_t menu = {0};
    const char *title;
    char value[24];

    camera_service_persist_defaults();
    CHECK(settings_menu_open(&menu, camera_settings_menu_definition()));
    CHECK(settings_menu_visible_slot(&menu, 0U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Capture") == 0 && strcmp(value, ">") == 0);
    CHECK(press(&menu, BUTTON_OK) == SETTINGS_MENU_EVENT_NONE);
    CHECK(strcmp(menu.definition->title, "CAM CAPTURE") == 0);
    CHECK(settings_menu_visible_slot(&menu, 0U, &title, NULL, 0U, NULL, NULL));
    CHECK(strcmp(title, "Size") == 0);
    press(&menu, BUTTON_OK);
    CHECK(camera_service_size() == camera_size_next(CAMERA_SIZE_320X240));
    CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_NONE);
    CHECK(menu.index == 0U && strcmp(menu.definition->title, "CAM SETTINGS") == 0);
    press(&menu, BUTTON_RIGHT);
    press(&menu, BUTTON_OK);
    CHECK(strcmp(menu.definition->title, "CAM LIGHTS") == 0);
    press(&menu, BUTTON_OK);
    CHECK(camera_service_light_mode() == CAMERA_LIGHT_RGB);
    CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_NONE && menu.index == 1U);
    CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_CLOSE_REQUESTED);
    settings_menu_close(&menu);

    CHECK(settings_menu_open(&menu, qr_settings_menu_definition()));
    CHECK(settings_menu_visible_slot(&menu, 0U, &title, NULL, 0U, NULL, NULL));
    CHECK(strcmp(title, "Decode Rate") == 0);
    settings_set_qr_decode_rate(QR_DECODE_RATE_MIN);
    press(&menu, BUTTON_OK);
    CHECK(settings_qr_decode_rate() == QR_DECODE_RATE_MIN + 1U);
    press(&menu, BUTTON_RIGHT);
    press(&menu, BUTTON_OK);
    CHECK(strcmp(menu.definition->title, "QR LIGHTS") == 0);
    CHECK(settings_menu_visible_slot(&menu, 0U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Source") == 0 && strcmp(value, "RGB") == 0);
    press(&menu, BUTTON_OK);
    CHECK(camera_service_light_mode() == CAMERA_LIGHT_LED);
    CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_NONE && menu.index == 1U);
    CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_CLOSE_REQUESTED);
    settings_menu_close(&menu);
    return 0;
}

static int test_tag_focal_editor(void)
{
    settings_menu_session_t menu = {0};
    const char *title;
    char value[24];
    const uint8_t sen0305 = strcmp(HK_BOARD_ID, "huskylens-sen0305") == 0;
    const uint32_t default_focal = sen0305 ? 154000U : 145000U;
    const char *default_label = sen0305 ? "154.00 px" : "145.00 px";
    const apriltag_distance_settings_t *distance = apriltag_settings_distance();
    s_has_accelerometer = !sen0305;
    CHECK(settings_menu_open(&menu, apriltag_settings_menu_definition()));
    CHECK(settings_menu_visible_slot(&menu, 0U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Detection") == 0 && strcmp(value, ">") == 0);
    CHECK(press(&menu, BUTTON_OK) == SETTINGS_MENU_EVENT_NONE);
    CHECK(settings_menu_visible_slot(&menu, 0U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Search") == 0 && strcmp(value, "NEAR") == 0);
    press(&menu, HK_INPUT_BUTTON_OK);
    CHECK(settings_menu_visible_slot(&menu, 0U, NULL, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(value, "FAR") == 0 && apriltag_settings_preferences()->far_search);
    CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_NONE);
    press(&menu, BUTTON_RIGHT);
    CHECK(press(&menu, BUTTON_OK) == SETTINGS_MENU_EVENT_NONE);
    CHECK(strcmp(menu.definition->title, "TAG DISTANCE") == 0);
    if(sen0305)
        CHECK(!settings_menu_visible_slot(&menu, 5U, NULL, NULL, 0U, NULL, NULL));
    else
    {
        press(&menu, BUTTON_LEFT);
        CHECK(settings_menu_visible_slot(&menu, 5U, &title, value, sizeof(value), NULL, NULL));
        CHECK(strcmp(title, "Level range") == 0 && strcmp(value, ">") == 0);
        CHECK(press(&menu, BUTTON_OK) == SETTINGS_MENU_EVENT_NONE);
        CHECK(strcmp(menu.definition->title, "LEVEL RANGE") == 0);
        CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_NONE);
        CHECK(strcmp(menu.definition->title, "TAG DISTANCE") == 0 && menu.index == 5U);
        CHECK(press(&menu, BUTTON_RIGHT) == SETTINGS_MENU_EVENT_NONE && menu.index == 0U);
    }
    for(uint8_t i = 0; i < 4U; i++)
        press(&menu, HK_INPUT_BUTTON_RIGHT);
    CHECK(settings_menu_visible_slot(&menu, 4U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Focal") == 0);
    CHECK(strcmp(value, default_label) == 0);
    press(&menu, HK_INPUT_BUTTON_OK);
    CHECK(settings_menu_visible_slot(&menu, 4U, NULL, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(value, default_label) == 0);
    press(&menu, HK_INPUT_BUTTON_BACK);
    CHECK(distance->focal_millipixels == default_focal);
    press(&menu, HK_INPUT_BUTTON_OK);
    press(&menu, HK_INPUT_BUTTON_OK);
    CHECK(distance->focal_millipixels == default_focal);
    press(&menu, HK_INPUT_BUTTON_OK);
    const hk_input_snapshot_t left_held = {HK_INPUT_BUTTON_LEFT, 0U, 0U};
    press(&menu, HK_INPUT_BUTTON_LEFT);
    int32_t largest_step = 0;
    for(unsigned i = 0; i < 250U; i++)
    {
        int32_t before = menu.edit_value;
        settings_menu_tick(&menu, &left_held);
        int32_t step = before - menu.edit_value;
        CHECK(step >= 0 && step <= 50);
        if(step > largest_step)
            largest_step = step;
    }
    CHECK(largest_step == 50);
    int32_t accelerated = menu.edit_value;
    press(&menu, HK_INPUT_BUTTON_RIGHT);
    CHECK(menu.edit_value == accelerated + 1);
    press(&menu, HK_INPUT_BUTTON_BACK);
    apriltag_settings_distance()->focal_millipixels = 153640U;
    press(&menu, HK_INPUT_BUTTON_OK);
    for(unsigned i = 0U; i < 4U; i++)
        press(&menu, HK_INPUT_BUTTON_LEFT);
    CHECK(settings_menu_visible_slot(&menu, 4U, NULL, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(value, "153.60 px") == 0);
    press(&menu, HK_INPUT_BUTTON_OK);
    CHECK(distance->focal_millipixels == 153600U);
    settings_storage_save_now();
    settings_store_load_t loaded;
    settings_store_init(&loaded);
    CHECK(loaded.has_payload && rd32(loaded.payload.tag_distance + 6U) == 153600U);
    CHECK(loaded.payload.app_data[1] & 0x08U);
    CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_NONE);
    CHECK(strcmp(menu.definition->title, "TAG SETTINGS") == 0 && menu.index == 1U);
    CHECK(press(&menu, BUTTON_BACK) == SETTINGS_MENU_EVENT_CLOSE_REQUESTED);
    return 0;
}

static uint32_t fixture_v4_crc(const fixture_record_v4_t *record)
{
    uint32_t crc = 0U;
    crc = crc32_update(crc, (const uint8_t *)&record->magic, sizeof(record->magic));
    crc = crc32_update(crc, (const uint8_t *)&record->version, sizeof(record->version));
    crc = crc32_update(crc, (const uint8_t *)&record->payload_size, sizeof(record->payload_size));
    crc = crc32_update(crc, (const uint8_t *)&record->sequence, sizeof(record->sequence));
    return crc32_update(crc, (const uint8_t *)&record->payload, sizeof(record->payload));
}

uint8_t hk_app_autostart_id_is_persistable(hk_autostart_id_t id)
{
    return id == HK_AUTOSTART_OFF || (id >= 1U && id <= 10U) || id == 300U;
}

internal_flash_result_t internal_flash_init(uint32_t hz)
{
    (void)hz;
    return INTERNAL_FLASH_OK;
}

void internal_flash_get_info(internal_flash_info_t *info)
{
    if(info)
    {
        memset(info, 0, sizeof(*info));
        info->jedec_id[0] = 0xEFU;
        info->jedec_id[1] = 0x40U;
        info->jedec_id[2] = 0x17U;
    }
}

static uint8_t *partition_bytes(internal_flash_partition_id_t partition)
{
    if(partition == INTERNAL_FLASH_PARTITION_SETTINGS_0)
        return s_flash[0];
    if(partition == INTERNAL_FLASH_PARTITION_SETTINGS_1)
        return s_flash[1];
    return NULL;
}

internal_flash_result_t internal_flash_read(
    internal_flash_partition_id_t partition, uint32_t offset,
    uint8_t *data, size_t len)
{
    uint8_t *source = partition_bytes(partition);
    if(!source || !data || offset + len > SETTINGS_FLASH_SLOT_SIZE)
        return INTERNAL_FLASH_ERROR_INVALID_ARGUMENT;
    memcpy(data, source + offset, len);
    return INTERNAL_FLASH_OK;
}

internal_flash_result_t internal_flash_erase(
    internal_flash_partition_id_t partition, uint32_t offset, size_t len)
{
    uint8_t *target = partition_bytes(partition);
    if(!target || offset + len > SETTINGS_FLASH_SLOT_SIZE)
        return INTERNAL_FLASH_ERROR_INVALID_ARGUMENT;
    memset(target + offset, 0xFF, len);
    return INTERNAL_FLASH_OK;
}

internal_flash_result_t internal_flash_program(
    internal_flash_partition_id_t partition, uint32_t offset,
    const uint8_t *data, size_t len)
{
    uint8_t *target = partition_bytes(partition);
    if(!target || !data || offset + len > SETTINGS_FLASH_SLOT_SIZE)
        return INTERNAL_FLASH_ERROR_INVALID_ARGUMENT;
    memcpy(target + offset, data, len);
    return INTERNAL_FLASH_OK;
}

const char *internal_flash_result_name(internal_flash_result_t result)
{
    return result == INTERNAL_FLASH_OK ? "ok" : "error";
}

int main(int argc, char **argv)
{
    if(argc == 2)
    {
        const uint32_t default_focal =
            strcmp(HK_BOARD_ID, "huskylens-sen0305") == 0 ? 154000U : 145000U;
        uint8_t stored[SETTINGS_TAG_DISTANCE_BYTES] = {1U};
        uint8_t unset = strcmp(argv[1], "--unset-focal") == 0;
        CHECK(unset || strcmp(argv[1], "--legacy-focal") == 0);
        if(!unset)
            wr32(stored + 6U, 153945U);
        settings_tag_distance_write(stored);
        CHECK(apriltag_settings_distance()->focal_millipixels ==
              (unset ? default_focal : 153950U));
        return 0;
    }
    settings_store_load_t loaded;
    settings_snapshot_t input;
    settings_snapshot_t output;
    settings_payload_t payload;
    fixture_record_v4_t v4;

    _Static_assert(sizeof(fixture_payload_v4_t) == 105U,
                   "fixture v4 payload layout changed");
    _Static_assert(sizeof(fixture_record_v4_t) == 124U,
                   "fixture v4 record layout changed");

    memset(s_flash, 0xFF, sizeof(s_flash));
    memset(&input, 0, sizeof(input));
    input.autostart_id = 300U;
    input.audio_volume = 30U;
    input.ui_color = HK_UI_COLOR_AMBER;
    input.tag_distance[0] = 1U;
    input.tag_distance[21] = 37U;
    input.app_data[SETTINGS_APP_DATA_V2_SIZE] = 0xD2U;
    input.object_disabled_classes = UINT32_C(1) << 19U;
    payload = settings_payload_encode(&input);
    CHECK(payload.autostart_id == 300U);

    settings_store_init(&loaded);
    CHECK(!loaded.has_payload);
    CHECK(settings_store_save(&payload));
    settings_store_init(&loaded);
    CHECK(loaded.has_payload && loaded.payload.autostart_id == 300U);
    settings_payload_decode(&loaded.payload, &output);
    CHECK(output.autostart_id == 300U);
    CHECK(output.audio_volume == 30U);
    CHECK(output.ui_color == HK_UI_COLOR_AMBER);
    CHECK(output.tag_distance[0] == 1U && output.tag_distance[21] == 37U);
    CHECK(output.object_disabled_classes == (UINT32_C(1) << 19U));
    CHECK(output.auto_sleep_minutes == 0U);

    memset(s_flash, 0xFF, sizeof(s_flash));
    settings_record_t v5 = {
        .magic = SETTINGS_MAGIC,
        .version = 5U,
        .payload_size = 106U,
        .sequence = 76U,
        .payload = payload,
    };
    wr32((uint8_t *)&v5 + 120U, crc32_update(0U, (const uint8_t *)&v5, 12U + v5.payload_size));
    memcpy(s_flash[0], &v5, sizeof(v5));
    settings_store_init(&loaded);
    CHECK(loaded.has_payload && loaded.payload.autostart_id == 300U);
    settings_payload_decode(&loaded.payload, &output);
    CHECK(output.audio_volume == SETTINGS_AUDIO_VOLUME_DEFAULT);
    CHECK(output.ui_color == HK_UI_COLOR_GREEN);

    settings_record_t v6 = v5;
    v6.version = SETTINGS_STORAGE_V6_VERSION;
    v6.payload_size = 108U;
    wr32((uint8_t *)&v6 + 120U, crc32_update(0U, (const uint8_t *)&v6, 12U + v6.payload_size));
    memcpy(s_flash[0], &v6, sizeof(v6));
    settings_store_init(&loaded);
    CHECK(loaded.has_payload);
    settings_payload_decode(&loaded.payload, &output);
    CHECK(output.autostart_id == 300U && output.audio_volume == 30U);
    CHECK(output.ui_color == HK_UI_COLOR_GREEN);
    v6.version = SETTINGS_STORAGE_V7_VERSION;
    wr32((uint8_t *)&v6 + 120U, crc32_update(0U, (const uint8_t *)&v6, 12U + v6.payload_size));
    memcpy(s_flash[0], &v6, sizeof(v6));
    settings_store_init(&loaded);
    CHECK(loaded.has_payload);
    settings_payload_decode(&loaded.payload, &output);
    CHECK(output.ui_color == HK_UI_COLOR_AMBER && output.tag_distance[0] == 0U);

    settings_record_t v8 = v6;
    v8.version = SETTINGS_STORAGE_V8_VERSION;
    v8.payload_size = 136U;
    v8.payload = payload;
    wr32((uint8_t *)&v8 + 148U, crc32_update(0U, (const uint8_t *)&v8, 12U + v8.payload_size));
    memcpy(s_flash[0], &v8, 152U);
    settings_storage_init();
    CHECK(settings_autostart_id() == 300U && settings_audio_volume() == 30U);
    CHECK(settings_ui_color() == HK_UI_COLOR_AMBER);
    settings_app_data_read(output.app_data);
    settings_tag_distance_read(output.tag_distance);
    CHECK(output.app_data[SETTINGS_APP_DATA_V2_SIZE] == 0xD2U);
    CHECK(output.tag_distance[0] == 1U && output.tag_distance[21] == 37U);
    CHECK(object_detect_settings_class_mask() == OBJECT_DETECT_ALL_CLASSES);

    settings_record_t v9 = v8;
    v9.version = SETTINGS_STORAGE_V9_VERSION;
    v9.payload_size = 140U;
    v9.payload = payload;
    wr32((uint8_t *)&v9 + 152U, crc32_update(0U, (const uint8_t *)&v9, 12U + v9.payload_size));
    memcpy(s_flash[0], &v9, 156U);
    settings_storage_init();
    CHECK(settings_object_disabled_classes() == (UINT32_C(1) << 19U));
    CHECK(settings_menu_order()->keys[0].kind == 0U);

    memset(s_flash, 0xFF, sizeof(s_flash));
    memset(&v4, 0, sizeof(v4));
    v4.magic = SETTINGS_MAGIC;
    v4.version = SETTINGS_STORAGE_V4_VERSION;
    v4.payload_size = sizeof(v4.payload);
    v4.sequence = 77U;
    v4.payload.autostart_id = 10U;
    v4.crc32 = fixture_v4_crc(&v4);
    memcpy(s_flash[0], &v4, sizeof(v4));
    settings_store_init(&loaded);
    CHECK(loaded.has_payload && loaded.payload.autostart_id == 10U);
    settings_payload_decode(&loaded.payload, &output);
    CHECK(output.autostart_id == 10U);
    CHECK(output.ui_color == HK_UI_COLOR_GREEN);

    settings_storage_init();
    CHECK(settings_ui_color() == HK_UI_COLOR_GREEN && hk_ui_accent() == 0x07e0U);
    CHECK(test_screen_and_device() == 0);
    CHECK(test_tag_focal_editor() == 0);
    CHECK(settings_autostart_id() == 10U);
    CHECK(test_object_classes() == 0);
    CHECK(test_camera_and_qr_groups() == 0);
    CHECK(test_menu_order() == 0);

    printf("SETTINGS_AUTOSTART_OK uint16=300 migrated_v4=10 record=%u\n",
           (unsigned)sizeof(settings_record_t));
    return 0;
}
