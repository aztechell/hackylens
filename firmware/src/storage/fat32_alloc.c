#include "internal/fat32_state_private.h"

#include <string.h>

#include "../config/sd_config.h"
#include "../config/fat32_config.h"
#include "fat32_allocation.h"

#include "../core/hk_binary.h"
#include "sd_card.h"

uint32_t fat_cluster_lba(uint32_t cluster)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    return geometry->data_begin + (cluster - 2U) * geometry->sectors_per_cluster;
}

uint32_t fat_next_cluster(uint32_t cluster)
{
    uint32_t next;
    return fat_read_next_cluster(cluster, &next) ? next : FAT32_EOC;
}

uint8_t fat_read_next_cluster(uint32_t cluster, uint32_t *next)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    uint8_t *sector_data = fat32_sector_scratch();
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector = geometry->fat_begin + fat_offset / SD_BLOCK_SIZE;
    uint16_t offset = (uint16_t)(fat_offset % SD_BLOCK_SIZE);

    if(cluster < 2U || cluster - 2U >= geometry->cluster_count ||
       !sd_card_read_block(sector, sector_data))
        return 0U;
    *next = rd32(&sector_data[offset]) & 0x0FFFFFFFUL;
    return 1U;
}

uint8_t fat_write_entry(uint32_t cluster, uint32_t value)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    uint8_t *sector_data = fat32_sector_scratch();
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector_offset = fat_offset / SD_BLOCK_SIZE;
    uint16_t offset = (uint16_t)(fat_offset % SD_BLOCK_SIZE);

    if(cluster < 2 || cluster >= geometry->cluster_count + 2U)
        return 0;

    for(uint8_t copy = 0; copy < geometry->fat_count; copy++)
    {
        uint32_t sector = geometry->fat_begin + (uint32_t)copy * geometry->sectors_per_fat + sector_offset;
        uint32_t old_value;
        if(!sd_card_read_block(sector, sector_data))
            return 0;
        old_value = rd32(&sector_data[offset]);
        wr32(&sector_data[offset], (old_value & 0xF0000000UL) | (value & 0x0FFFFFFFUL));
        if(!sd_card_write_block(sector, sector_data))
            return 0;
    }

    return 1;
}

uint8_t fat_find_free_cluster(uint32_t *cluster_out)
{
    fat_cluster_search_t search = {0};
    hk_result_t result;
    do
        result = fat_find_free_step(&search, cluster_out);
    while(result == HK_PENDING);
    return result == HK_OK;
}

hk_result_t fat_find_free_step(fat_cluster_search_t *search, uint32_t *cluster_out)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    const uint32_t end = geometry->cluster_count + 2U;
    const uint32_t entries_per_sector = SD_BLOCK_SIZE / 4U;
    uint8_t *sector = fat32_sector_scratch();

    if(!geometry->cluster_count)
        return HK_ERR_IO;
    if(!search->next)
    {
        search->next = geometry->alloc_hint >= 2U && geometry->alloc_hint < end ?
            geometry->alloc_hint : 2U;
        search->remaining = geometry->cluster_count;
    }
    if(!search->remaining)
        return HK_ERR_LIMIT;
    uint32_t sector_index = search->next / entries_per_sector;
    if(!sd_card_read_block(geometry->fat_begin + sector_index, sector))
        return HK_ERR_IO;
    while(search->remaining && search->next < end &&
          search->next / entries_per_sector == sector_index)
    {
        uint32_t cluster = search->next++;
        search->remaining--;
        if(!(rd32(sector + (cluster % entries_per_sector) * 4U) & 0x0fffffffU))
        {
            *cluster_out = cluster;
            fat32_set_alloc_hint(cluster + 1U);
            return HK_OK;
        }
    }
    if(search->next == end)
        search->next = 2U;
    return search->remaining ? HK_PENDING : HK_ERR_LIMIT;
}

/* Camera files commonly occupy a contiguous run. Update each FAT sector once
 * per mirror, with the usual write/readback verification. */
static hk_result_t fat_check_free_run(uint32_t first, uint32_t end)
{
    const uint32_t per_sector = SD_BLOCK_SIZE / 4U;
    uint8_t *sector = fat32_sector_scratch();
    while(first < end)
    {
        uint32_t index = first / per_sector;
        if(!sd_card_read_block(fat32_geometry()->fat_begin + index, sector))
            return HK_ERR_IO;
        do
        {
            if(rd32(sector + (first % per_sector) * 4U) & 0x0fffffffUL)
                return HK_ERR_BUSY;
            first++;
        } while(first < end && first / per_sector == index);
    }
    return HK_OK;
}

static uint8_t fat_write_run(uint32_t first, uint32_t end, uint8_t clear)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    const uint32_t per_sector = SD_BLOCK_SIZE / 4U;
    uint8_t *sector = fat32_sector_scratch();
    for(uint8_t copy = 0; copy < geometry->fat_count; copy++)
    {
        uint32_t base = geometry->fat_begin + (uint32_t)copy * geometry->sectors_per_fat;
        for(uint32_t cluster = first; cluster < end;)
        {
            uint32_t index = cluster / per_sector;
            if(!sd_card_read_block(base + index, sector))
                return 0U;
            do
            {
                uint8_t *entry = sector + (cluster % per_sector) * 4U;
                uint32_t next = cluster + 1U == end ? FAT32_EOC : cluster + 1U;
                wr32(entry, (rd32(entry) & 0xf0000000UL) | (clear ? 0U : next));
                cluster++;
            } while(cluster < end && cluster / per_sector == index);
            if(!sd_card_write_block(base + index, sector))
                return 0U;
        }
    }
    return 1U;
}

static hk_result_t fat_alloc_run(uint32_t count, uint32_t *first_out)
{
    uint32_t first;
    const uint32_t limit = fat32_geometry()->cluster_count + 2U;
    if(!fat_find_free_cluster(&first))
        return HK_ERR_IO;
    fat32_set_alloc_hint(first);
    if(count > limit - first)
        return HK_ERR_BUSY;
    uint32_t end = first + count;
    hk_result_t result = fat_check_free_run(first, end);
    if(result != HK_OK)
        return result;
    if(!fat_write_run(first, end, 0U))
    {
        (void)fat_write_run(first, end, 1U);
        return HK_ERR_IO;
    }
    fat32_set_alloc_hint(end);
    *first_out = first;
    return HK_OK;
}

uint8_t fat_alloc_chain(uint32_t count, uint32_t *first_out)
{
    uint32_t first = 0U;
    uint32_t previous = 0U;

    if(!first_out || count == 0U || count > fat32_geometry()->cluster_count)
        return 0U;
    if(count > 1U)
    {
        hk_result_t run = fat_alloc_run(count, first_out);
        if(run != HK_ERR_BUSY)
            return run == HK_OK;
    }

    for(uint32_t i = 0; i < count; i++)
    {
        uint32_t cluster;
        if(!fat_find_free_cluster(&cluster))
            goto failed;
        if(!fat_write_entry(cluster, FAT32_EOC))
        {
            (void)fat_write_entry(cluster, 0U);
            goto failed;
        }
        if(previous && !fat_write_entry(previous, cluster))
        {
            /* The primary FAT link may have been written before the mirror failed. */
            (void)fat_write_entry(previous, FAT32_EOC);
            (void)fat_write_entry(cluster, 0U);
            goto failed;
        }
        if(first == 0U)
            first = cluster;
        previous = cluster;
    }

    *first_out = first;
    return 1U;

failed:
    if(first)
        (void)fat_free_chain(first);
    return 0U;
}


uint8_t fat_free_chain(uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;

    while(cluster >= 2 && cluster < FAT32_EOC)
    {
        uint32_t next = fat_next_cluster(cluster);
        if(!fat_write_entry(cluster, 0))
            return 0;
        if(next >= FAT32_EOC || next < 2)
            break;
        cluster = next;
    }

    if(first_cluster >= 2 && first_cluster < fat32_geometry()->alloc_hint)
        fat32_set_alloc_hint(first_cluster);
    return 1;
}

uint8_t fat_zero_cluster(uint32_t cluster)
{
    const fat32_geometry_t *geometry = fat32_geometry();
    uint8_t *sector_data = fat32_sector_scratch();

    memset(sector_data, 0, SD_BLOCK_SIZE);
    for(uint8_t s = 0; s < geometry->sectors_per_cluster; s++)
    {
        if(!sd_card_write_block(fat_cluster_lba(cluster) + s, sector_data))
            return 0;
    }
    return 1;
}
