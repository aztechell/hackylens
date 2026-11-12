#include "fat32_append.h"

#include <string.h>

#include "../config/fat32_config.h"
#include "fat32_directory.h"
#include "fat32_volume.h"
#include "sd_card.h"

#define FAT_APPEND_RUN_SECTORS 4U

static hk_result_t write_entry(const fat_append_t *file, uint32_t bytes)
{
    uint8_t entry[32];
    fat_make_short_entry(entry, file->name, 0x20U, file->first_cluster, bytes);
    return fat_dir_write_slots(&file->slot, entry, 1U) ? HK_OK : HK_ERR_IO;
}

hk_result_t fat_append_begin(fat_append_t *file, uint32_t dir, const char *base)
{
    memset(file, 0, sizeof(*file));
    fat_make_short_name(file->name, base, "TMP");
    if(!hk_fat_mounted() || !fat_dir_find_slots(dir, 1U, &file->slot))
        return HK_ERR_IO;
    return write_entry(file, 0U);
}

static hk_result_t append_cluster(fat_append_t *file)
{
    uint32_t cluster = file->next_cluster;
    file->next_cluster = 0U;
    /* A prefetch is only a hint. Another storage user may have claimed it. */
    if(!cluster || fat_next_cluster(cluster) != 0U)
    {
        hk_result_t result = fat_find_free_step(&file->search, &cluster);
        if(result != HK_OK)
            return result;
    }
    if(!fat_write_entry(cluster, FAT32_EOC))
        return HK_ERR_IO;
    if(file->cluster)
    {
        if(!fat_write_entry(file->cluster, cluster))
            return HK_ERR_IO;
    }
    else
    {
        file->first_cluster = cluster;
        if(write_entry(file, 0U) != HK_OK)
            return HK_ERR_IO;
    }
    file->cluster = cluster;
    file->sector_index = 0U;
    file->search = (fat_cluster_search_t){0};
    return HK_OK;
}

hk_result_t fat_append_prepare(fat_append_t *file)
{
    if(!hk_fat_mounted())
        return HK_ERR_IO;
    return file->cluster ? HK_OK : append_cluster(file);
}

hk_result_t fat_append_prefetch(fat_append_t *file)
{
    if(!hk_fat_mounted())
        return HK_ERR_IO;
    if(!file->cluster || file->next_cluster)
        return HK_OK;
    return fat_find_free_step(&file->search, &file->next_cluster);
}

hk_result_t fat_append_flush(fat_append_t *file)
{
    if(!file->buffered)
        return HK_OK;
    if(!hk_fat_mounted())
        return HK_ERR_IO;
    if(UINT32_MAX - file->committed < file->buffered)
        return HK_ERR_LIMIT;
    if(!file->cluster || file->sector_index >= fat32_sectors_per_cluster())
    {
        hk_result_t result = append_cluster(file);
        if(result != HK_OK)
            return result;
    }
    memset(file->sector + file->buffered, 0, SD_BLOCK_SIZE - file->buffered);
    if(!sd_card_write_block_fast(fat_cluster_lba(file->cluster) + file->sector_index,
                                 file->sector))
        return HK_ERR_IO;
    file->committed += file->buffered;
    file->sector_index++;
    file->buffered = 0U;
    return HK_OK;
}

hk_result_t fat_append_write(fat_append_t *file, const uint8_t *data,
                             size_t bytes, size_t *accepted)
{
    size_t take = SD_BLOCK_SIZE - file->buffered;
    if(take > bytes)
        take = bytes;
    if(take)
        memcpy(file->sector + file->buffered, data, take);
    file->buffered += (uint16_t)take;
    *accepted = take;
    return file->buffered == SD_BLOCK_SIZE ? fat_append_flush(file) : HK_OK;
}

hk_result_t fat_append_write_run(fat_append_t *file, uint32_t remaining,
                                 sd_block_source_t source, void *context, size_t *accepted)
{
    *accepted = 0U;
    if(!hk_fat_mounted() || file->buffered || !remaining || !source)
        return HK_ERR_INVALID_STATE;
    if(remaining > UINT32_MAX - file->committed)
        return HK_ERR_LIMIT;
    uint8_t sectors = fat32_sectors_per_cluster();
    if(!sectors)
        return HK_ERR_IO;
    if(!file->cluster || file->sector_index >= sectors)
    {
        hk_result_t result = append_cluster(file);
        if(result != HK_OK)
            return result;
    }
    uint16_t count = sectors - file->sector_index;
    uint32_t needed = 1U + (remaining - 1U) / SD_BLOCK_SIZE;
    if(count > needed)
        count = (uint16_t)needed;
    if(count > FAT_APPEND_RUN_SECTORS)
        count = FAT_APPEND_RUN_SECTORS;
    if(!sd_card_write_run(fat_cluster_lba(file->cluster) + file->sector_index,
                          count, source, context))
        return HK_ERR_IO;
    uint32_t bytes = (uint32_t)count * SD_BLOCK_SIZE;
    if(bytes > remaining)
        bytes = remaining;
    file->committed += bytes;
    file->sector_index += count;
    *accepted = bytes;
    return HK_OK;
}

hk_result_t fat_append_publish(fat_append_t *file, const char *ext,
                               const uint8_t *header, size_t header_size)
{
    if(!hk_fat_mounted() ||
       header_size > SD_BLOCK_SIZE || header_size > file->committed)
        return HK_ERR_IO;
    if(header_size)
    {
        if(!file->first_cluster || !header)
            return HK_ERR_IO;
        uint32_t lba = fat_cluster_lba(file->first_cluster);
        if(!sd_card_read_block(lba, file->sector))
            return HK_ERR_IO;
        memcpy(file->sector, header, header_size);
        if(!sd_card_write_block(lba, file->sector))
            return HK_ERR_IO;
    }
    memcpy(file->name + 8U, ext, 3U);
    return write_entry(file, file->committed);
}

hk_result_t fat_append_discard_empty(fat_append_t *file)
{
    if(!hk_fat_mounted() || file->committed)
        return HK_ERR_IO;
    uint8_t entry[32] = {0xe5U};
    if(!fat_dir_write_slots(&file->slot, entry, 1U))
        return HK_ERR_IO;
    if(file->first_cluster && !fat_free_chain(file->first_cluster))
        return HK_ERR_IO;
    return HK_OK;
}

hk_result_t fat_append_discard_step(fat_append_t *file)
{
    if(!hk_fat_mounted())
        return HK_ERR_IO;
    if(file->slot.cluster)
    {
        const uint8_t entry[32] = {0xe5U};
        if(!fat_dir_write_slots(&file->slot, entry, 1U))
            return HK_ERR_IO;
        file->slot.cluster = 0U;
    }
    uint32_t cluster = file->first_cluster;
    if(cluster < 2U || cluster >= FAT32_EOC)
        return HK_OK;
    uint32_t next;
    if(!fat_read_next_cluster(cluster, &next) || !fat_write_entry(cluster, 0U))
        return HK_ERR_IO;
    file->first_cluster = next;
    return next >= 2U && next < FAT32_EOC ? HK_PENDING : HK_OK;
}
