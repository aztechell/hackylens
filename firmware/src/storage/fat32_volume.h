#ifndef HK_FAT32_VOLUME_H
#define HK_FAT32_VOLUME_H

#include <stdint.h>

typedef enum
{
    FAT32_USAGE_UNAVAILABLE,
    FAT32_USAGE_SCANNING,
    FAT32_USAGE_READY,
    FAT32_USAGE_ERROR,
} fat32_usage_status_t;

typedef struct
{
    fat32_usage_status_t status;
    uint8_t progress;
    uint64_t used_bytes;
    uint64_t total_bytes;
} fat32_usage_t;

uint8_t fat32_mount(void);
uint8_t hk_sd_present(void);
uint8_t hk_fat_mounted(void);
uint32_t fat32_mount_generation(void);
uint32_t hk_fat_root_cluster(void);
uint8_t fat32_sectors_per_cluster(void);
uint32_t fat32_cluster_size(void);
void fat32_mark_removed(void);
void fat32_set_probe_silent(uint8_t silent);
uint8_t fat32_probe_silent(void);
uint8_t fat32_health_check(void);
fat32_usage_t fat32_usage(void);
uint8_t fat32_usage_scan_step(void);
void fat32_usage_retry(void);

#endif
