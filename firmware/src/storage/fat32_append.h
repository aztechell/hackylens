#ifndef HK_FAT32_APPEND_H
#define HK_FAT32_APPEND_H

#include <stddef.h>
#include <stdint.h>
#include <hackylens/capability/common.h>

#include "../config/sd_config.h"
#include "fat32_allocation.h"
#include "fat32_types.h"
#include "sd_card.h"

typedef struct
{
    fat_dir_slot_t slot;
    fat_cluster_search_t search;
    uint32_t first_cluster;
    uint32_t cluster;
    uint32_t next_cluster;
    uint32_t committed;
    uint16_t buffered;
    uint8_t sector_index;
    uint8_t name[11];
    uint8_t sector[SD_BLOCK_SIZE];
} fat_append_t;

hk_result_t fat_append_begin(fat_append_t *file, uint32_t dir, const char *base);
hk_result_t fat_append_prepare(fat_append_t *file);
hk_result_t fat_append_prefetch(fat_append_t *file);
hk_result_t fat_append_write(fat_append_t *file, const uint8_t *data,
                             size_t bytes, size_t *accepted);
/* Writes a short run from a sector generator, without buffering a second copy. */
hk_result_t fat_append_write_run(fat_append_t *file, uint32_t remaining,
                                 sd_block_source_t source, void *context, size_t *accepted);
hk_result_t fat_append_flush(fat_append_t *file);
hk_result_t fat_append_publish(fat_append_t *file, const char *ext,
                               const uint8_t *header, size_t header_size);
hk_result_t fat_append_discard_empty(fat_append_t *file);
/* Unlink the temporary entry, then free at most one cluster per call. */
hk_result_t fat_append_discard_step(fat_append_t *file);

#endif
