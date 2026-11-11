#include "face_gallery.h"

#include <stdio.h>
#include <string.h>

#include "../config/fat32_config.h"
#include "../config/sd_config.h"
#include "../core/hk_binary.h"
#include "fat32_allocation.h"
#include "fat32_directory.h"
#include "fat32_file.h"
#include "file_path.h"
#include "sd_card.h"

#define RECORD_MAGIC UINT32_C(0x31465248)
#define RECORD_HEADER 16U
#define RECORD_BYTES (RECORD_HEADER + FACE_GALLERY_DIM)

static file_path_result_t find_record(uint8_t id, fat_file_entry_t *entry)
{
    char path[sizeof(FACE_GALLERY_DIRECTORY "/FR000.DAT")];
    snprintf(path, sizeof(path), FACE_GALLERY_DIRECTORY "/FR%02u.DAT", (unsigned)id);
    return file_path_find(path, entry);
}

static uint16_t id_bit(uint8_t id)
{
    return (uint16_t)(1U << (id - 1U));
}

face_gallery_result_t face_gallery_load(face_gallery_t *gallery, uint32_t model_crc)
{
    fat_file_entry_t entry;
    uint8_t record[RECORD_BYTES];
    memset(gallery, 0, sizeof(*gallery));
    if(file_path_find(FACE_GALLERY_DIRECTORY, &entry) != FILE_PATH_OK ||
       !(entry.attr & FAT_ATTR_DIR))
        return FACE_GALLERY_IO;
    for(uint8_t id = 1U; id <= FACE_GALLERY_CAPACITY; id++)
    {
        file_path_result_t result = find_record(id, &entry);
        if(result == FILE_PATH_NOT_FOUND)
            continue;
        if(result != FILE_PATH_OK || (entry.attr & FAT_ATTR_DIR))
            return FACE_GALLERY_IO;
        if(entry.size == sizeof(record))
        {
            if(!fat_file_read_at(&entry, 0U, record, sizeof(record)))
                return FACE_GALLERY_IO;
            if(rd32(record) == RECORD_MAGIC && rd32(record + 4U) == model_crc &&
               rd16(record + 8U) == id && rd16(record + 10U) == FACE_GALLERY_DIM &&
               rd32(record + 12U) == crc32_update(0U, record + RECORD_HEADER, FACE_GALLERY_DIM))
            {
                memcpy(gallery->features[id - 1U], record + RECORD_HEADER, FACE_GALLERY_DIM);
                gallery->occupied |= id_bit(id);
                continue;
            }
        }
        gallery->invalid |= id_bit(id);
    }
    return gallery->invalid ? FACE_GALLERY_CORRUPT : FACE_GALLERY_OK;
}

face_gallery_result_t face_gallery_store(face_gallery_t *gallery, uint8_t id,
                                         uint32_t model_crc, const int8_t *feature)
{
    fat_file_entry_t directory, entry;
    fat_dir_slot_t slot;
    uint32_t cluster;
    uint8_t sector[SD_BLOCK_SIZE] = {0};
    uint8_t raw_entry[32], short_name[11];
    char base[6];
    if(id == 0U || id > FACE_GALLERY_CAPACITY)
        return FACE_GALLERY_FULL;
    if(file_path_find(FACE_GALLERY_DIRECTORY, &directory) != FILE_PATH_OK ||
       !(directory.attr & FAT_ATTR_DIR))
        return FACE_GALLERY_IO;
    file_path_result_t existing = find_record(id, &entry);
    if(existing != FILE_PATH_NOT_FOUND)
        return existing == FILE_PATH_OK ? FACE_GALLERY_OCCUPIED : FACE_GALLERY_IO;
    if(!fat_dir_find_slots(directory.cluster, 1U, &slot))
        return FACE_GALLERY_IO;
    if(!fat_alloc_chain(1U, &cluster))
        return FACE_GALLERY_FULL;

    wr32(sector, RECORD_MAGIC);
    wr32(sector + 4U, model_crc);
    wr16(sector + 8U, id);
    wr16(sector + 10U, FACE_GALLERY_DIM);
    memcpy(sector + RECORD_HEADER, feature, FACE_GALLERY_DIM);
    wr32(sector + 12U, crc32_update(0U, sector + RECORD_HEADER, FACE_GALLERY_DIM));
    if(!sd_card_write_block(fat_cluster_lba(cluster), sector))
    {
        (void)fat_free_chain(cluster);
        return FACE_GALLERY_IO;
    }
    snprintf(base, sizeof(base), "FR%02u", (unsigned)id);
    fat_make_short_name(short_name, base, "DAT");
    fat_make_short_entry(raw_entry, short_name, 0x20U, cluster, RECORD_BYTES);
    if(!fat_dir_write_slots(&slot, raw_entry, 1U))
        return FACE_GALLERY_IO;
    memcpy(gallery->features[id - 1U], feature, FACE_GALLERY_DIM);
    gallery->occupied |= id_bit(id);
    return FACE_GALLERY_OK;
}

face_gallery_result_t face_gallery_forget(face_gallery_t *gallery, uint8_t id)
{
    fat_file_entry_t directory, entry;
    if(id == 0U || id > FACE_GALLERY_CAPACITY ||
       file_path_find(FACE_GALLERY_DIRECTORY, &directory) != FILE_PATH_OK ||
       !(directory.attr & FAT_ATTR_DIR))
        return FACE_GALLERY_IO;
    file_path_result_t found = find_record(id, &entry);
    if(found == FILE_PATH_OK && !(entry.attr & FAT_ATTR_DIR))
    {
        if(!fat_dir_mark_deleted(directory.cluster, entry.dir_ordinal, entry.lfn_count))
            return FACE_GALLERY_IO;
        if(entry.cluster >= 2U)
            (void)fat_free_chain(entry.cluster);
    }
    else if(found != FILE_PATH_NOT_FOUND)
        return FACE_GALLERY_IO;
    gallery->occupied &= (uint16_t)~id_bit(id);
    gallery->invalid &= (uint16_t)~id_bit(id);
    memset(gallery->features[id - 1U], 0, FACE_GALLERY_DIM);
    return FACE_GALLERY_OK;
}
