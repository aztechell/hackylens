#include "internal/fat32_state_private.h"

#include "../config/sd_config.h"
#include "fat32_volume.h"

#include "../core/hk_binary.h"
#include "sd_card.h"

static uint8_t g_sd_present;
static uint8_t g_sd_sdhc;
static uint8_t g_sd_probe_silent;
static uint8_t g_fat_mounted;
static uint32_t g_fat_mount_generation;
static uint32_t g_fat_lba_begin;
static uint32_t g_fat_fat_begin;
static uint32_t g_fat_data_begin;
static uint32_t g_fat_root_cluster;
static uint32_t g_fat_sectors_per_fat;
static uint32_t g_fat_total_sectors;
static uint32_t g_fat_cluster_count;
static uint32_t g_fat_alloc_hint = 2;
static uint8_t g_fat_sectors_per_cluster;
static uint8_t g_fat_count;
static uint32_t g_usage_scanned;
static uint32_t g_usage_allocated;
static uint8_t g_usage_error;
static uint8_t g_sd_sector[SD_BLOCK_SIZE] __attribute__((aligned(4)));
static uint8_t g_sd_verify_sector[SD_BLOCK_SIZE] __attribute__((aligned(4)));

uint8_t hk_sd_present(void)
{
    return g_sd_present;
}

uint8_t hk_fat_mounted(void)
{
    return g_fat_mounted;
}

uint32_t fat32_mount_generation(void)
{
    return g_fat_mount_generation;
}

uint32_t hk_fat_root_cluster(void)
{
    return g_fat_root_cluster;
}

uint8_t fat32_sectors_per_cluster(void)
{
    return g_fat_sectors_per_cluster;
}

uint32_t fat32_cluster_size(void)
{
    return (uint32_t)g_fat_sectors_per_cluster * SD_BLOCK_SIZE;
}

void fat32_usage_invalidate(void)
{
    g_usage_scanned = 0U;
    g_usage_allocated = 0U;
    g_usage_error = 0U;
}

void fat32_usage_retry(void)
{
    g_usage_error = 0U;
}

void fat32_usage_note_entry(uint32_t cluster, uint32_t old_value, uint32_t new_value)
{
    uint32_t old_allocated = (old_value & 0x0fffffffU) != 0U;
    uint32_t new_allocated = (new_value & 0x0fffffffU) != 0U;
    if(old_allocated == new_allocated)
        return;
    if(cluster - 2U < g_usage_scanned)
        g_usage_allocated = g_usage_allocated - old_allocated + new_allocated;
}

fat32_usage_t fat32_usage(void)
{
    fat32_usage_t usage = {0};
    if(!g_fat_mounted || !g_fat_cluster_count)
        return usage;

    usage.total_bytes = (uint64_t)g_fat_cluster_count * fat32_cluster_size();
    if(g_usage_scanned == g_fat_cluster_count)
    {
        usage.status = FAT32_USAGE_READY;
        usage.progress = 100U;
        usage.used_bytes = (uint64_t)g_usage_allocated * fat32_cluster_size();
    }
    else
    {
        usage.status = g_usage_error ? FAT32_USAGE_ERROR : FAT32_USAGE_SCANNING;
        usage.progress = (uint8_t)((uint64_t)g_usage_scanned * 100U /
                                   g_fat_cluster_count);
    }
    return usage;
}

uint8_t fat32_usage_scan_step(void)
{
    const uint32_t entries_per_sector = SD_BLOCK_SIZE / 4U;
    if(!g_fat_mounted || g_usage_error || g_usage_scanned == g_fat_cluster_count)
        return 0U;

    uint32_t cluster = g_usage_scanned + 2U;
    uint32_t sector = cluster / entries_per_sector;
    if(sector >= g_fat_sectors_per_fat ||
       !sd_card_read_block(g_fat_fat_begin + sector, g_sd_sector))
    {
        g_usage_error = 1U;
        return 0U;
    }

    for(uint32_t i = cluster % entries_per_sector;
        i < entries_per_sector && g_usage_scanned < g_fat_cluster_count; i++)
    {
        g_usage_allocated += (rd32(g_sd_sector + i * 4U) & 0x0fffffffU) != 0U;
        g_usage_scanned++;
    }
    return g_usage_scanned < g_fat_cluster_count;
}

uint8_t *fat32_sector_scratch(void)
{
    return g_sd_sector;
}

uint8_t fat32_card_is_sdhc(void)
{
    return g_sd_sdhc;
}

void fat32_card_reset_state(void)
{
    g_sd_present = 0;
    g_sd_sdhc = 0;
    g_fat_mounted = 0;
}

void fat32_card_set_sdhc(uint8_t sdhc)
{
    g_sd_sdhc = sdhc ? 1 : 0;
}

void fat32_card_set_present(uint8_t present)
{
    g_sd_present = present ? 1 : 0;
}

uint8_t *fat32_verify_scratch(void)
{
    return g_sd_verify_sector;
}

void fat32_mark_removed(void)
{
    fat32_card_reset_state();
}

void fat32_set_probe_silent(uint8_t silent)
{
    g_sd_probe_silent = silent ? 1 : 0;
}

uint8_t fat32_probe_silent(void)
{
    return g_sd_probe_silent;
}

const fat32_geometry_t *fat32_geometry(void)
{
    static fat32_geometry_t geometry;

    geometry.fat_begin = g_fat_fat_begin;
    geometry.data_begin = g_fat_data_begin;
    geometry.sectors_per_fat = g_fat_sectors_per_fat;
    geometry.cluster_count = g_fat_cluster_count;
    geometry.alloc_hint = g_fat_alloc_hint;
    geometry.sectors_per_cluster = g_fat_sectors_per_cluster;
    geometry.fat_count = g_fat_count;
    return &geometry;
}

void fat32_set_mounted(const fat32_mount_info_t *mount)
{
    g_fat_lba_begin = mount->lba_begin;
    g_fat_fat_begin = mount->fat_begin;
    g_fat_data_begin = mount->data_begin;
    g_fat_root_cluster = mount->root_cluster;
    g_fat_sectors_per_fat = mount->sectors_per_fat;
    g_fat_total_sectors = mount->total_sectors;
    g_fat_cluster_count = mount->cluster_count;
    g_fat_sectors_per_cluster = mount->sectors_per_cluster;
    g_fat_count = mount->fat_count;
    g_fat_alloc_hint = 2;
    fat32_usage_invalidate();
    g_fat_mount_generation++;
    g_fat_mounted = 1;
}

void fat32_set_alloc_hint(uint32_t cluster)
{
    g_fat_alloc_hint = cluster;
}

uint8_t fat32_health_check(void)
{
    if(!g_sd_present || !g_fat_mounted)
        return 0;
    if(!sd_card_read_block(g_fat_lba_begin, g_sd_sector))
        return 0;
    if(g_sd_sector[510] != 0x55 || g_sd_sector[511] != 0xAA)
        return 0;
    return rd16(&g_sd_sector[11]) == SD_BLOCK_SIZE;
}
