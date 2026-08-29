#include <stdio.h>
#include <string.h>

#include "../firmware/src/config/settings_config.h"
#include "../firmware/src/core/hk_app_registry.h"
#include "../firmware/src/core/hk_binary.h"
#include "../firmware/src/services/settings_payload_codec.h"
#include "../firmware/src/storage/internal_flash.h"
#include "../firmware/src/storage/settings_store.h"

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

int main(void)
{
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
    payload = settings_payload_encode(&input);
    CHECK(payload.autostart_id == 300U);

    settings_store_init(&loaded);
    CHECK(!loaded.has_payload);
    CHECK(settings_store_save(&payload));
    settings_store_init(&loaded);
    CHECK(loaded.has_payload && loaded.payload.autostart_id == 300U);
    settings_payload_decode(&loaded.payload, &output);
    CHECK(output.autostart_id == 300U);

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

    puts("SETTINGS_AUTOSTART_OK uint16=300 migrated_v4=10 record=124");
    return 0;
}
