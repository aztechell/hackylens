#include "../../storage/file_write_error.h"

#include <stddef.h>

#include <string.h>

#include "../../core/photo_types.h"

#include "../../config/sd_config.h"
#include "../../config/fat32_config.h"
#include "../../storage/file_mount.h"
#include "../../storage/fat32_allocation.h"
#include "../../storage/fat32_volume.h"
#include "../../storage/internal/fat32_state_private.h"
#include "camera_photo_encode.h"
#include "camera_photo_path.h"
#include "camera_photo_writer.h"
#include "../../storage/sd_card.h"

typedef struct
{
    const photo_encoder_t *encoder;
    uint32_t offset;
    uint32_t size;
} photo_write_run_t;

static const uint8_t *photo_write_sector(void *context, uint16_t index)
{
    const photo_write_run_t *run = context;
    uint32_t offset = run->offset + (uint32_t)index * SD_BLOCK_SIZE;
    uint32_t available = run->size - offset;
    uint16_t take = available > SD_BLOCK_SIZE ? SD_BLOCK_SIZE : (uint16_t)available;
    uint8_t *sector = fat32_sector_scratch();

    if(take < SD_BLOCK_SIZE)
        memset(sector + take, 0, SD_BLOCK_SIZE - take);
    photo_encode_fill_file_bytes(run->encoder, offset, sector, take);
    return sector;
}

static uint8_t photo_write_file_chain(uint32_t cluster, const photo_encoder_t *encoder,
                                      uint32_t file_size)
{
    uint16_t sectors_per_cluster = fat32_sectors_per_cluster();
    photo_write_run_t run = {encoder, 0U, file_size};

    if(!sectors_per_cluster)
        return 0U;
    while(run.offset < file_size && cluster >= 2U && cluster < FAT32_EOC)
    {
        uint32_t remaining = file_size - run.offset;
        uint32_t sectors = (remaining + SD_BLOCK_SIZE - 1U) / SD_BLOCK_SIZE;
        uint16_t count = sectors > sectors_per_cluster ? sectors_per_cluster : (uint16_t)sectors;
        if(!sd_card_write_run(fat_cluster_lba(cluster), count, photo_write_sector, &run))
            return 0U;
        run.offset += (uint32_t)count * SD_BLOCK_SIZE;
        if(run.offset < file_size)
            cluster = fat_next_cluster(cluster);
    }
    return run.offset >= file_size;
}

uint8_t photo_save_frame(const photo_source_t *source, char *saved_name, size_t saved_name_size)
{
    uint8_t short_name[11];
    uint32_t first_cluster;
    uint32_t cluster_size;
    uint32_t cluster_count;
    uint32_t file_size;

    if(!source || !source->read_pixel || source->width == 0 || source->height == 0)
    {
        file_write_set_error("NO FRAME");
        return 0;
    }

    file_write_clear_error();
    photo_encoder_t encoder;
    file_size = photo_encode_begin(&encoder, source);

    if(!file_mount_if_needed())
    {
        file_write_set_error("MOUNT");
        return 0;
    }
    if(!photo_path_prepare(short_name, saved_name, saved_name_size, source->format))
    {
        if(file_write_last_error() == NULL)
            file_write_set_error("NAME");
        return 0;
    }

    cluster_size = fat32_cluster_size();
    if(cluster_size == 0)
    {
        file_write_set_error("CLUSTER");
        return 0;
    }
    cluster_count = (file_size + cluster_size - 1U) / cluster_size;
    if(!fat_alloc_chain(cluster_count, &first_cluster))
    {
        file_write_set_error("ALLOC");
        return 0;
    }
    if(!photo_write_file_chain(first_cluster, &encoder, file_size))
    {
        (void)fat_free_chain(first_cluster);
        file_write_set_error("WRITE");
        return 0;
    }
    if(!photo_path_add_file_entry(short_name, first_cluster, file_size))
    {
        if(file_write_last_error() == NULL)
            file_write_set_error("DIRENT");
        return 0;
    }

    file_write_clear_error();
    return 1U;
}
