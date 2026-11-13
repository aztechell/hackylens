#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/src/config/fat32_config.h"
#include "../firmware/src/core/hk_binary.h"
#include "../firmware/src/services/camera_photo_encode.h"
#include "../firmware/src/services/resource_cleanup.h"
#include "../firmware/src/services/script_sd_store.h"
#include "../firmware/src/storage/fat32_file.h"
#include "../firmware/src/storage/fat32_allocation.h"
#include "../firmware/src/storage/fat32_volume.h"
#include "../firmware/src/storage/file_path.h"
#include "../firmware/src/storage/internal/fat32_state_private.h"

#define SECTOR_BYTES 512U
#define FAT_SECTOR 1U
#define DATA_SECTOR 2U
#define CLUSTERS 20U

static uint8_t disk[DATA_SECTOR + 4U * CLUSTERS][SECTOR_BYTES];
static uint8_t replacement[sizeof(disk)];
static uint8_t fail_data_write;
static uint8_t fail_fat_read;
static uint8_t fail_fat_write;
static uint16_t last_run_count;
static const fat32_mount_info_t geometry = {
    .fat_begin = FAT_SECTOR, .data_begin = DATA_SECTOR, .root_cluster = 2U,
    .sectors_per_fat = 1U, .total_sectors = DATA_SECTOR + CLUSTERS,
    .cluster_count = CLUSTERS, .sectors_per_cluster = 1U, .fat_count = 1U,
};

uint8_t sd_card_read_block(uint32_t lba, uint8_t *data)
{
    assert(lba < sizeof(disk) / SECTOR_BYTES);
    if(!hk_fat_mounted() || (lba == FAT_SECTOR && fail_fat_read))
        return 0U;
    memcpy(data, disk[lba], SECTOR_BYTES);
    return 1U;
}

uint8_t sd_card_write_block(uint32_t lba, const uint8_t *data)
{
    assert(lba < sizeof(disk) / SECTOR_BYTES);
    if(!hk_fat_mounted() || (lba == FAT_SECTOR && fail_fat_write))
        return 0U;
    memcpy(disk[lba], data, SECTOR_BYTES);
    return 1U;
}

uint8_t sd_card_write_block_fast(uint32_t lba, const uint8_t *data)
{
    return fail_data_write ? 0U : sd_card_write_block(lba, data);
}

uint8_t sd_card_write_run(uint32_t lba, uint16_t count,
                          sd_block_source_t source, void *context)
{
    last_run_count = count;
    for(uint16_t i = 0U; i < count; i++)
        if(!sd_card_write_block_fast(lba + i, source(context, i)))
            return 0U;
    return 1U;
}

uint8_t file_mount_if_needed(void) { return hk_fat_mounted(); }

static void mount_blank(void)
{
    memset(disk, 0, sizeof(disk));
    wr32(disk[FAT_SECTOR] + 2U * 4U, FAT32_EOC);
    fat32_set_mounted(&geometry);
}

static void test_volume_usage(void)
{
    fat32_mount_info_t two_sectors = geometry;
    two_sectors.sectors_per_fat = 2U;
    two_sectors.data_begin = FAT_SECTOR + 2U;
    two_sectors.cluster_count = 129U;
    two_sectors.sectors_per_cluster = 64U;
    two_sectors.total_sectors = two_sectors.data_begin + 129U * 64U;
    memset(disk, 0, sizeof(disk));
    wr32(disk[FAT_SECTOR], FAT32_EOC); /* Reserved entries are outside capacity. */
    wr32(disk[FAT_SECTOR] + 8U, FAT32_EOC);
    wr32(disk[FAT_SECTOR] + 12U, 0xa0000000U); /* Only the low 28 bits count. */
    wr32(disk[FAT_SECTOR + 1U] + 12U, FAT32_EOC); /* Past the last cluster. */
    fat32_set_mounted(&two_sectors);
    fat32_usage_t usage = fat32_usage();
    assert(usage.status == FAT32_USAGE_SCANNING && usage.total_bytes == 129U * 32768U);
    assert(fat32_usage_scan_step());
    assert(fat32_usage().progress == 97U && fat32_usage().used_bytes == 0U);
    assert(fat_write_entry(3U, FAT32_EOC)); /* Already counted sector. */
    assert(fat_write_entry(128U, FAT32_EOC)); /* Sector still to scan. */
    fail_fat_read = 1U;
    assert(!fat32_usage_scan_step() && fat32_usage().status == FAT32_USAGE_READY);
    fail_fat_read = 0U;
    assert(fat32_usage().used_bytes == 3U * 32768U);
    assert(fat_write_entry(128U, 0U) && fat32_usage().used_bytes == 2U * 32768U);
    uint32_t first = 0U;
    assert(fat_alloc_chain(4U, &first) && fat32_usage().used_bytes == 6U * 32768U);
    assert(fat_free_chain(first) && fat32_usage().used_bytes == 2U * 32768U);

    fail_fat_write = 1U;
    assert(!fat_write_entry(3U, 0U) && fat32_usage().status == FAT32_USAGE_SCANNING);
    fail_fat_write = 0U;
    fail_fat_read = 1U;
    assert(!fat32_usage_scan_step() && fat32_usage().status == FAT32_USAGE_ERROR);
    fail_fat_read = 0U;
    fat32_usage_retry();
    assert(fat32_usage_scan_step());
    assert(!fat32_usage_scan_step() && fat32_usage().used_bytes == 2U * 32768U);
    fat32_mark_removed();
    assert(fat32_usage().status == FAT32_USAGE_UNAVAILABLE);
    mount_blank();
    assert(!fat32_usage_scan_step());
    assert(fat32_usage().used_bytes == SECTOR_BYTES);
}

static script_sd_result_t begin(const char *name, uint8_t bmp)
{
    for(unsigned retry = 0; retry < 10U; retry++)
    {
        script_sd_result_t result = script_sd_begin(name, strlen(name), bmp);
        if(result != SCRIPT_SD_PENDING)
            return result;
    }
    assert(0 && "directory preparation did not finish");
    return SCRIPT_SD_IO;
}

static script_sd_result_t write_all(const uint8_t *data, size_t size)
{
    for(unsigned retry = 0; retry < 100U && size; retry++)
    {
        size_t accepted;
        size_t count = size < 193U ? size : 193U;
        script_sd_result_t result = script_sd_write(data, count, &accepted);
        assert(accepted <= count);
        if(result != SCRIPT_SD_OK && result != SCRIPT_SD_PENDING)
            return result;
        data += accepted;
        size -= accepted;
    }
    assert(!size);
    return SCRIPT_SD_OK;
}

static void finish(void)
{
    script_sd_result_t result;
    do
        result = script_sd_finish();
    while(result == SCRIPT_SD_PENDING);
    assert(result == SCRIPT_SD_OK);
}

static uint8_t allocated(void)
{
    uint8_t count = 0U;
    for(uint32_t cluster = 2U; cluster < CLUSTERS + 2U; cluster++)
        count += rd32(disk[FAT_SECTOR] + cluster * 4U) != 0U;
    return count;
}

static uint16_t photo_pixel(void *context, uint32_t x, uint32_t y)
{
    const uint16_t *pixels = context;
    return pixels[y * 3U + x];
}

static const uint8_t *source_sector(void *context, uint16_t index)
{
    const uint8_t *data = context;
    return data + (uint32_t)index * SECTOR_BYTES;
}

int main(void)
{
    fat_file_entry_t entry;
    uint8_t payload[1407];
    uint8_t readback[sizeof(payload)];
    for(size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(i * 29U);

    assert(begin("DATA.BIN", 0U) == SCRIPT_SD_NO_MEDIA);
    mount_blank();
    const char *invalid[] = {"../A.BIN", "/A.BIN", "A\\B.BIN", "A..BIN", "A.TMP", "LONGNAME9.BIN"};
    for(size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        assert(begin(invalid[i], 0U) == SCRIPT_SD_INVALID_NAME);
    assert(script_sd_begin("A\0.BIN", 6U, 0U) == SCRIPT_SD_INVALID_NAME);
    assert(begin("BAD.RAW", 1U) == SCRIPT_SD_INVALID_NAME);
    assert(allocated() == 1U);

    assert(begin("data.bin", 0U) == SCRIPT_SD_OK);
    assert(write_all(payload, sizeof(payload)) == SCRIPT_SD_OK);
    assert(file_path_find("/PYDATA/DATA.BIN", &entry) == FILE_PATH_NOT_FOUND);
    assert(file_path_find("/PYDATA/DATA.TMP", &entry) == FILE_PATH_OK);
    finish();
    assert(file_path_find("/PYDATA/DATA.TMP", &entry) == FILE_PATH_NOT_FOUND);
    assert(file_path_find("/PYDATA/DATA.BIN", &entry) == FILE_PATH_OK);
    assert(entry.size == sizeof(payload));
    assert(fat_file_read_at(&entry, 0U, readback, sizeof(readback)));
    assert(memcmp(readback, payload, sizeof(payload)) == 0);
    assert(begin("DATA.BIN", 0U) == SCRIPT_SD_EXISTS);
    assert(file_path_find("/DATA.BIN", &entry) == FILE_PATH_NOT_FOUND);

    assert(begin("EMPTY.X", 0U) == SCRIPT_SD_OK);
    finish();
    assert(file_path_find("/PYDATA/EMPTY.X", &entry) == FILE_PATH_OK);
    assert(entry.size == 0U && entry.cluster == 0U);

    uint16_t pixels[] = {0xf800, 0x07e0, 0x001f, 0xffff, 0x0000, 0xffe0};
    photo_source_t source = {PHOTO_FORMAT_BMP24, 3U, 2U, photo_pixel, pixels};
    photo_encoder_t encoder;
    uint32_t size = photo_encode_begin(&encoder, &source);
    assert(size == 78U && begin("CAM.BMP", 1U) == SCRIPT_SD_OK);
    for(uint32_t offset = 0U; offset < size; offset += 13U)
    {
        uint8_t bytes[13];
        uint16_t count = size - offset < sizeof(bytes) ? size - offset : sizeof(bytes);
        photo_encode_fill_file_bytes(&encoder, offset, bytes, count);
        assert(write_all(bytes, count) == SCRIPT_SD_OK);
    }
    finish();
    assert(file_path_find("/PYDATA/CAM.BMP", &entry) == FILE_PATH_OK);
    assert(entry.size == size && fat_file_read_at(&entry, 0U, readback, size));
    assert(memcmp(readback, "BM", 2U) == 0 && rd32(readback + 2U) == size);
    assert(rd32(readback + 18U) == 3U && rd32(readback + 22U) == 2U);
    assert(memcmp(readback + 54U, "\xff\xff\xff\0\0\0\0\xff\xff\0\0\0", 12U) == 0);
    assert(memcmp(readback + 66U, "\0\0\xff\0\xff\0\xff\0\0\0\0\0", 12U) == 0);

    uint8_t baseline = allocated();
    assert(begin("STOP.BIN", 0U) == SCRIPT_SD_OK);
    assert(write_all(payload, sizeof(payload)) == SCRIPT_SD_OK);
    script_sd_abort();
    assert(file_path_find("/PYDATA/STOP.TMP", &entry) == FILE_PATH_NOT_FOUND);
    assert(file_path_find("/PYDATA/STOP.BIN", &entry) == FILE_PATH_NOT_FOUND);
    for(unsigned i = 0U; i < 4U; i++)
        resource_cleanup_poll();
    assert(allocated() == baseline);
    assert(begin("STOP.BIN", 0U) == SCRIPT_SD_OK);
    fail_data_write = 1U;
    assert(write_all(payload, sizeof(payload)) == SCRIPT_SD_IO);
    fail_data_write = 0U;
    assert(file_path_find("/PYDATA/STOP.BIN", &entry) == FILE_PATH_NOT_FOUND);
    assert(allocated() == baseline);

    for(uint32_t cluster = 2U; cluster < CLUSTERS + 2U; cluster++)
        if(rd32(disk[FAT_SECTOR] + cluster * 4U) == 0U)
            wr32(disk[FAT_SECTOR] + cluster * 4U, FAT32_EOC);
    assert(begin("FULL.BIN", 0U) == SCRIPT_SD_OK);
    assert(write_all(payload, sizeof(payload)) == SCRIPT_SD_FULL);
    assert(file_path_find("/PYDATA/FULL.TMP", &entry) == FILE_PATH_NOT_FOUND);

    mount_blank();
    assert(begin("CARD.BIN", 0U) == SCRIPT_SD_OK);
    assert(write_all(payload, sizeof(payload)) == SCRIPT_SD_OK);
    fat32_mark_removed();
    mount_blank();
    memcpy(replacement, disk, sizeof(disk));
    assert(script_sd_finish() == SCRIPT_SD_NO_MEDIA);
    resource_cleanup_poll();
    assert(memcmp(replacement, disk, sizeof(disk)) == 0);

    fat32_mount_info_t larger_clusters = geometry;
    larger_clusters.sectors_per_cluster = 4U;
    larger_clusters.total_sectors = DATA_SECTOR + 4U * CLUSTERS;
    fat32_set_mounted(&larger_clusters);
    uint8_t padded[3U * SECTOR_BYTES] = {0};
    memcpy(padded, payload, sizeof(payload));
    assert(begin("RUN.BIN", 0U) == SCRIPT_SD_OK);
    size_t accepted;
    assert(script_sd_write_run(sizeof(payload), source_sector, padded, &accepted) == SCRIPT_SD_OK);
    assert(last_run_count == 3U && accepted == sizeof(payload));
    finish();
    assert(file_path_find("/PYDATA/RUN.BIN", &entry) == FILE_PATH_OK);
    assert(entry.size == sizeof(payload) && fat_file_read_at(&entry, 0U, readback, entry.size));
    assert(memcmp(readback, payload, sizeof(payload)) == 0);
    test_volume_usage();
    puts("SCRIPT_SD_STORE_OK");
    return 0;
}
