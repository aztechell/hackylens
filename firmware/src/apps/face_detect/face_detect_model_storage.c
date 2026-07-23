#include "face_detect_model_storage.h"

#include <stdlib.h>

#include "../../config/fat32_config.h"
#include "../../storage/fat32_file.h"
#include "../../storage/fat32_volume.h"
#include "../../storage/file_mount.h"
#include "../../storage/file_path.h"

#define FACE_MODEL_DIR_PATH "/hackylens.kmodels"
#define FACE_MODEL_FILE_PATH "/hackylens.kmodels/detect.kmodel"
#define FACE_MODEL_MAX_BYTES (1024U * 1024U)

face_model_storage_result_t face_detect_model_storage_load(uint8_t **buffer, uint32_t *size)
{
    fat_file_entry_t entry;
    uint8_t *raw;
    uint8_t *data;

    if(buffer) *buffer = NULL;
    if(size) *size = 0;
    if(!hk_sd_present() || !file_mount_if_needed() || !hk_fat_mounted())
        return FACE_MODEL_STORAGE_NO_SD;
    if(file_path_find(FACE_MODEL_DIR_PATH, &entry) != FILE_PATH_OK || !(entry.attr & FAT_ATTR_DIR))
        return FACE_MODEL_STORAGE_DIR;
    if(file_path_find(FACE_MODEL_FILE_PATH, &entry) != FILE_PATH_OK || (entry.attr & FAT_ATTR_DIR))
        return FACE_MODEL_STORAGE_FILE;
    if(entry.size < 28U || entry.size > FACE_MODEL_MAX_BYTES)
        return FACE_MODEL_STORAGE_READ;
    /* K210 KPU model bodies are consumed through 64-byte DMA blocks. The
       reference model is emitted with 128-byte alignment; preserve that
       contract for a container loaded from FAT instead of trusting malloc. */
    raw = (uint8_t *)malloc(entry.size + 128U + sizeof(void *));
    if(!raw)
        return FACE_MODEL_STORAGE_ALLOC;
    data = (uint8_t *)(((uintptr_t)(raw + sizeof(void *) + 127U)) & ~(uintptr_t)127U);
    ((void **)data)[-1] = raw;
    if(!fat_file_read_at(&entry, 0, data, entry.size))
    {
        free(raw);
        return FACE_MODEL_STORAGE_READ;
    }
    if(buffer) *buffer = data;
    if(size) *size = entry.size;
    return FACE_MODEL_STORAGE_OK;
}

void face_detect_model_storage_free(uint8_t *buffer)
{
    if(buffer)
        free(((void **)buffer)[-1]);
}
