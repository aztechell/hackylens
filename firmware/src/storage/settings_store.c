#include "settings_store.h"

#include <stdio.h>
#include <string.h>

#include "settings_store_types.h"

#include "../config/settings_config.h"

#include "../core/hk_binary.h"
#include "../drivers/boot_flash.h"

static uint8_t g_settings_storage_ok;
static uint8_t g_settings_storage_slot = 0xFF;
static uint32_t g_settings_sequence;

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
} settings_payload_v1_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t sequence;
    settings_payload_v1_t payload;
    uint32_t crc32;
} settings_record_v1_t;

typedef union
{
    settings_record_t current;
    settings_record_v1_t legacy;
} settings_slot_t;

_Static_assert(sizeof(settings_payload_v1_t) == 16U, "legacy settings payload layout changed");
_Static_assert(sizeof(settings_record_v1_t) == 32U, "legacy settings record layout changed");

static uint32_t flash_slot_addr(uint8_t slot)
{
    return slot ? SETTINGS_FLASH_SLOT1 : SETTINGS_FLASH_SLOT0;
}

static uint32_t settings_record_crc(const settings_record_t *record)
{
    uint32_t crc = 0;
    crc = crc32_update(crc, (const uint8_t *)&record->magic, sizeof(record->magic));
    crc = crc32_update(crc, (const uint8_t *)&record->version, sizeof(record->version));
    crc = crc32_update(crc, (const uint8_t *)&record->payload_size, sizeof(record->payload_size));
    crc = crc32_update(crc, (const uint8_t *)&record->sequence, sizeof(record->sequence));
    crc = crc32_update(crc, (const uint8_t *)&record->payload, sizeof(record->payload));
    return crc;
}

static uint8_t settings_record_valid(const settings_record_t *record)
{
    if(record->magic != SETTINGS_MAGIC)
        return 0;
    if(record->version != SETTINGS_STORAGE_VERSION)
        return 0;
    if(record->payload_size != sizeof(settings_payload_t))
        return 0;
    return record->crc32 == settings_record_crc(record);
}

static uint32_t settings_record_v1_crc(const settings_record_v1_t *record)
{
    uint32_t crc = 0;
    crc = crc32_update(crc, (const uint8_t *)&record->magic, sizeof(record->magic));
    crc = crc32_update(crc, (const uint8_t *)&record->version, sizeof(record->version));
    crc = crc32_update(crc, (const uint8_t *)&record->payload_size, sizeof(record->payload_size));
    crc = crc32_update(crc, (const uint8_t *)&record->sequence, sizeof(record->sequence));
    crc = crc32_update(crc, (const uint8_t *)&record->payload, sizeof(record->payload));
    return crc;
}

static uint8_t settings_record_v1_valid(const settings_record_v1_t *record)
{
    return record->magic == SETTINGS_MAGIC &&
           record->version == SETTINGS_STORAGE_LEGACY_VERSION &&
           record->payload_size == sizeof(settings_payload_v1_t) &&
           record->crc32 == settings_record_v1_crc(record);
}

static uint8_t settings_slot_valid(const settings_slot_t *slot)
{
    if(slot->current.version == SETTINGS_STORAGE_VERSION)
        return settings_record_valid(&slot->current);
    return settings_record_v1_valid(&slot->legacy);
}

static uint32_t settings_slot_sequence(const settings_slot_t *slot)
{
    return slot->current.version == SETTINGS_STORAGE_VERSION ?
           slot->current.sequence : slot->legacy.sequence;
}

static void settings_slot_payload(const settings_slot_t *slot, settings_payload_t *payload)
{
    memset(payload, 0, sizeof(*payload));
    if(slot->current.version == SETTINGS_STORAGE_VERSION)
        *payload = slot->current.payload;
    else
        memcpy(payload, &slot->legacy.payload, sizeof(slot->legacy.payload));
}

void settings_store_init(settings_store_load_t *result)
{
    settings_slot_t slots[2];
    uint8_t jedec[3] = {0};
    uint8_t valid0;
    uint8_t valid1;
    uint8_t selected = 0xFF;

    if(result)
        memset(result, 0, sizeof(*result));

    boot_flash_init(10000000U);
    boot_flash_read_id(jedec);
    printf("[SETTINGS] flash jedec=%02X%02X%02X\r\n", jedec[0], jedec[1], jedec[2]);

    if(jedec[0] == 0x00 || jedec[0] == 0xFF || jedec[2] < SETTINGS_FLASH_MIN_CAPACITY_LOG2)
    {
        printf("[SETTINGS] persistence disabled\r\n");
        g_settings_storage_ok = 0;
        return;
    }

    boot_flash_read(SETTINGS_FLASH_SLOT0, (uint8_t *)&slots[0], sizeof(settings_slot_t));
    boot_flash_read(SETTINGS_FLASH_SLOT1, (uint8_t *)&slots[1], sizeof(settings_slot_t));
    valid0 = settings_slot_valid(&slots[0]);
    valid1 = settings_slot_valid(&slots[1]);

    if(valid0 && valid1)
        selected = settings_slot_sequence(&slots[1]) > settings_slot_sequence(&slots[0]) ? 1 : 0;
    else if(valid0)
        selected = 0;
    else if(valid1)
        selected = 1;

    g_settings_storage_ok = 1;
    if(selected != 0xFF)
    {
        if(result)
        {
            result->has_payload = 1;
            settings_slot_payload(&slots[selected], &result->payload);
        }
        g_settings_sequence = settings_slot_sequence(&slots[selected]);
        g_settings_storage_slot = selected;
        printf("[SETTINGS] loaded slot=%u seq=%u schema=v%u\r\n", selected,
               g_settings_sequence, slots[selected].current.version);
    }
    else
    {
        printf("[SETTINGS] defaults\r\n");
    }
}

uint8_t settings_store_enabled(void)
{
    return g_settings_storage_ok;
}

uint8_t settings_store_save(const settings_payload_t *payload)
{
    settings_record_t record;
    settings_record_t verify;
    uint8_t target;

    if(!g_settings_storage_ok || !payload)
        return 0;

    memset(&record, 0xFF, sizeof(record));
    record.magic = SETTINGS_MAGIC;
    record.version = SETTINGS_STORAGE_VERSION;
    record.payload_size = sizeof(settings_payload_t);
    record.sequence = g_settings_sequence + 1;
    record.payload = *payload;
    record.crc32 = settings_record_crc(&record);

    target = g_settings_storage_slot == 0 ? 1 : 0;
    if(g_settings_storage_slot == 0xFF)
        target = 0;

    boot_flash_sector_erase(flash_slot_addr(target));
    boot_flash_page_program(flash_slot_addr(target), (const uint8_t *)&record, sizeof(record));
    boot_flash_read(flash_slot_addr(target), (uint8_t *)&verify, sizeof(verify));

    if(settings_record_valid(&verify) && verify.sequence == record.sequence)
    {
        g_settings_sequence = record.sequence;
        g_settings_storage_slot = target;
        printf("[SETTINGS] saved slot=%u seq=%u\r\n", target, g_settings_sequence);
        return 1;
    }

    printf("[SETTINGS] save verify failed slot=%u\r\n", target);
    return 0;
}
