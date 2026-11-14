#include <stdio.h>
#include <string.h>

#include "../firmware/src/config/settings_config.h"
#include "../firmware/src/config/input_config.h"
#include "../firmware/src/apps/apriltag/apriltag_settings.h"
#include "../firmware/src/apps/apriltag/apriltag_settings_menu.h"
#include "../firmware/src/services/camera_session_preferences.h"
#include "../firmware/src/apps/settings/settings_menu.h"
#include "../firmware/src/core/hk_ui_colors.h"
#include "../firmware/src/core/hk_app_registry.h"
#include "../firmware/src/core/hk_binary.h"
#include "../firmware/src/services/settings_payload_codec.h"
#include "../firmware/src/services/settings_persistence.h"
#include "../firmware/src/services/settings_app_data.h"
#include "../firmware/src/services/settings_service.h"
#include "../firmware/src/storage/internal_flash.h"
#include "../firmware/src/storage/settings_store.h"
#include "../firmware/src/storage/fat32_volume.h"

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
uint8_t apriltag_controller_arm_calibration(void) { return 0U; }
uint8_t apriltag_controller_calibration_armed(void) { return 0U; }
void apriltag_controller_cancel_calibration(void) {}
uint8_t apriltag_controller_has_accelerometer(void) { return 0U; }
uint8_t apriltag_controller_capture_gravity(uint8_t lens_down) { (void)lens_down; return 0U; }
void settings_menu_view_open(const char *title) { (void)title; }
void settings_menu_view_clear_rows(void) {}
void settings_menu_view_draw_row(uint8_t slot, const char *title, const char *value,
                                 uint8_t selected, uint8_t editing)
{
    (void)slot;
    (void)title;
    (void)value;
    (void)selected;
    (void)editing;
}

static void press(settings_menu_session_t *menu, uint32_t button)
{
    const hk_input_snapshot_t input = {button, button, button};
    (void)settings_menu_handle_input(menu, &input);
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

static int test_tag_focal_editor(void)
{
    settings_menu_session_t menu = {0};
    const char *title;
    char value[24];
    const uint8_t sen0305 = strcmp(HK_BOARD_ID, "huskylens-sen0305") == 0;
    const uint32_t default_focal = sen0305 ? 154000U : 0U;
    const uint32_t editor_focal = sen0305 ? 154000U : 120000U;
    const apriltag_distance_settings_t *distance = apriltag_settings_distance();
    CHECK(settings_menu_bind(&menu, apriltag_settings_menu_definition(&menu)));
    CHECK(settings_menu_visible_slot(&menu, 0U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Search") == 0 && strcmp(value, "NEAR") == 0);
    press(&menu, HK_INPUT_BUTTON_OK);
    CHECK(settings_menu_visible_slot(&menu, 0U, NULL, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(value, "FAR") == 0 && apriltag_settings_preferences()->far_search);
    for(uint8_t i = 0; i < 5U; i++)
        press(&menu, HK_INPUT_BUTTON_RIGHT);
    CHECK(settings_menu_visible_slot(&menu, 5U, &title, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(title, "Focal") == 0);
    CHECK(strcmp(value, sen0305 ? "154.00 px" : "UNSET") == 0);
    press(&menu, HK_INPUT_BUTTON_OK);
    CHECK(settings_menu_visible_slot(&menu, 5U, NULL, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(value, sen0305 ? "154.00 px" : "120.00 px") == 0);
    press(&menu, HK_INPUT_BUTTON_BACK);
    CHECK(distance->focal_millipixels == default_focal);
    press(&menu, HK_INPUT_BUTTON_OK);
    press(&menu, HK_INPUT_BUTTON_OK);
    CHECK(distance->focal_millipixels == editor_focal);
    press(&menu, HK_INPUT_BUTTON_OK);
    const hk_input_snapshot_t left_held = {HK_INPUT_BUTTON_LEFT, 0U, 0U};
    press(&menu, HK_INPUT_BUTTON_LEFT);
    for(unsigned i = 0; i < 200U; i++)
        settings_menu_tick(&menu, &left_held);
    int32_t accelerated = menu.edit_value;
    CHECK(accelerated < (int32_t)(editor_focal / 10U) - 1000);
    press(&menu, HK_INPUT_BUTTON_RIGHT);
    CHECK(menu.edit_value == accelerated + 1);
    press(&menu, HK_INPUT_BUTTON_BACK);
    apriltag_settings_distance()->focal_millipixels = 153640U;
    press(&menu, HK_INPUT_BUTTON_OK);
    for(unsigned i = 0U; i < 4U; i++)
        press(&menu, HK_INPUT_BUTTON_LEFT);
    CHECK(settings_menu_visible_slot(&menu, 5U, NULL, value, sizeof(value), NULL, NULL));
    CHECK(strcmp(value, "153.60 px") == 0);
    press(&menu, HK_INPUT_BUTTON_OK);
    CHECK(distance->focal_millipixels == 153600U);
    settings_storage_save_now();
    settings_store_load_t loaded;
    settings_store_init(&loaded);
    CHECK(loaded.has_payload && rd32(loaded.payload.tag_distance + 6U) == 153600U);
    CHECK(loaded.payload.app_data[1] & 0x08U);
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
    if(argc == 2 && strcmp(argv[1], "--legacy-focal") == 0)
    {
        uint8_t stored[SETTINGS_TAG_DISTANCE_BYTES] = {1U};
        wr32(stored + 6U, 153945U);
        settings_tag_distance_write(stored);
        CHECK(apriltag_settings_distance()->focal_millipixels == 153950U);
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

    puts("SETTINGS_AUTOSTART_OK uint16=300 migrated_v4=10 record=152");
    return 0;
}
