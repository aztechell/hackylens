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

void settings_store_init(settings_store_load_t *result)
{
    settings_record_t slots[2];
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

    boot_flash_read(SETTINGS_FLASH_SLOT0, (uint8_t *)&slots[0], sizeof(settings_record_t));
    boot_flash_read(SETTINGS_FLASH_SLOT1, (uint8_t *)&slots[1], sizeof(settings_record_t));
    valid0 = settings_record_valid(&slots[0]);
    valid1 = settings_record_valid(&slots[1]);

    if(valid0 && valid1)
        selected = slots[1].sequence > slots[0].sequence ? 1 : 0;
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
            result->payload = slots[selected].payload;
        }
        g_settings_sequence = slots[selected].sequence;
        g_settings_storage_slot = selected;
        printf("[SETTINGS] loaded slot=%u seq=%u\r\n", selected, g_settings_sequence);
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
