#include "script_sd_store.h"

#include <stdio.h>
#include <string.h>

#include "../config/fat32_config.h"
#include "../core/hk_string.h"
#include "../storage/fat32_append.h"
#include "../storage/fat32_directory.h"
#include "../storage/fat32_volume.h"
#include "../storage/file_dir_scan.h"
#include "../storage/file_mount.h"
#include "resource_cleanup.h"

#define SCRIPT_SD_DIRECTORY "PYDATA"

typedef enum
{
    WRITER_IDLE,
    WRITER_PREPARING,
    WRITER_OPEN,
    WRITER_ABORTING,
} writer_state_t;

static struct
{
    writer_state_t state;
    uint32_t generation;
    fat_dir_slot_t directory_slot;
    fat_cluster_search_t directory_search;
    char base[9];
    char extension[4];
    fat_append_t file;
} s_writer;

typedef struct
{
    const char *name;
    uint8_t short_name[11];
    uint8_t found;
    uint32_t directory;
} name_lookup_t;

static uint8_t find_name(const fat_file_entry_t *entry,
                         const uint8_t short_name[11], void *context)
{
    name_lookup_t *lookup = context;
    uint8_t long_match = str_eq_ci(entry->name, lookup->name);
    uint8_t short_match = memcmp(short_name, lookup->short_name, 11U) == 0;
    if(!long_match && !short_match)
        return 0U;
    lookup->found = 1U;
    if(long_match && short_match && (entry->attr & FAT_ATTR_DIR) &&
       !(entry->attr & FAT_ATTR_READONLY) && entry->cluster >= 2U &&
       entry->cluster < FAT32_EOC && entry->cluster != hk_fat_root_cluster())
        lookup->directory = entry->cluster;
    return 1U;
}

static uint8_t same_media(void)
{
    return hk_fat_mounted() && fat32_mount_generation() == s_writer.generation;
}

static uint8_t discard_step(void)
{
    if(same_media() && fat_append_discard_step(&s_writer.file) == HK_PENDING)
        return 0U;
    s_writer.state = WRITER_IDLE;
    return 1U;
}

void script_sd_abort(void)
{
    if(s_writer.state == WRITER_PREPARING)
        s_writer.state = WRITER_IDLE;
    if(s_writer.state == WRITER_OPEN)
    {
        s_writer.state = WRITER_ABORTING;
        if(!discard_step())
            (void)resource_cleanup_schedule(RESOURCE_CLEANUP_SCRIPT_SD, discard_step);
    }
}

static uint8_t split_name(const char *name, size_t length, uint8_t bmp_only,
                          char base[9], char extension[4])
{
    if(!name || length < 3U || length > SCRIPT_SD_NAME_MAX)
        return 0U;
    size_t dot = 0U;
    for(size_t i = 0U; i < length; i++)
    {
        char c = name[i];
        if(c == '.' && !dot && i && i <= 8U)
            dot = i;
        else if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') || c == '_' || c == '-'))
            return 0U;
    }
    if(!dot || length - dot < 2U || length - dot > 4U)
        return 0U;
    memcpy(base, name, dot);
    base[dot] = '\0';
    memcpy(extension, name + dot + 1U, length - dot - 1U);
    extension[length - dot - 1U] = '\0';
    return !str_eq_ci(extension, "TMP") && (!bmp_only || str_eq_ci(extension, "BMP"));
}

static script_sd_result_t storage_result(hk_result_t result)
{
    if(result == HK_PENDING)
        return SCRIPT_SD_PENDING;
    if(result == HK_OK)
        return SCRIPT_SD_OK;
    return result == HK_ERR_LIMIT ? SCRIPT_SD_FULL : SCRIPT_SD_IO;
}

static script_sd_result_t prepare_directory(uint32_t *directory)
{
    const uint32_t root = hk_fat_root_cluster();
    name_lookup_t lookup = {.name = SCRIPT_SD_DIRECTORY};
    fat_make_short_name(lookup.short_name, SCRIPT_SD_DIRECTORY, "");
    if(!s_writer.directory_slot.cluster)
    {
        if(!file_dir_scan(root, find_name, &lookup))
            return SCRIPT_SD_IO;
        if(lookup.found)
        {
            uint32_t next;
            if(!lookup.directory || !fat_read_next_cluster(lookup.directory, &next) || !next)
                return SCRIPT_SD_IO;
            *directory = lookup.directory;
            return SCRIPT_SD_OK;
        }
        if(!fat_dir_find_slots(root, 1U, &s_writer.directory_slot))
            return SCRIPT_SD_IO;
    }
    uint32_t cluster;
    hk_result_t result = fat_find_free_step(&s_writer.directory_search, &cluster);
    if(result != HK_OK)
        return storage_result(result);
    if(!fat_write_entry(cluster, FAT32_EOC))
        return SCRIPT_SD_IO;
    if(!fat_init_directory_cluster(cluster, root))
    {
        (void)fat_write_entry(cluster, 0U);
        return SCRIPT_SD_IO;
    }
    uint8_t entry[32];
    fat_make_short_entry(entry, lookup.short_name, FAT_ATTR_DIR, cluster, 0U);
    if(!fat_dir_write_slots(&s_writer.directory_slot, entry, 1U))
        return SCRIPT_SD_IO;
    *directory = cluster;
    return SCRIPT_SD_OK;
}

static script_sd_result_t create_temporary(uint32_t directory)
{
    const char *extensions[] = {s_writer.extension, "TMP"};
    for(size_t i = 0U; i < sizeof(extensions) / sizeof(extensions[0]); i++)
    {
        char name[SCRIPT_SD_NAME_MAX + 1U];
        snprintf(name, sizeof(name), "%s.%s", s_writer.base, extensions[i]);
        name_lookup_t lookup = {.name = name};
        fat_make_short_name(lookup.short_name, s_writer.base, extensions[i]);
        if(!file_dir_scan(directory, find_name, &lookup))
            return SCRIPT_SD_IO;
        if(lookup.found)
            return SCRIPT_SD_EXISTS;
    }
    return storage_result(fat_append_begin(&s_writer.file, directory, s_writer.base));
}

script_sd_result_t script_sd_begin(const char *name, size_t length, uint8_t bmp_only)
{
    if(s_writer.state == WRITER_OPEN || s_writer.state == WRITER_ABORTING)
        return SCRIPT_SD_BUSY;
    if(s_writer.state == WRITER_IDLE)
    {
        if(!split_name(name, length, bmp_only, s_writer.base, s_writer.extension))
            return SCRIPT_SD_INVALID_NAME;
        if(!file_mount_if_needed())
            return SCRIPT_SD_NO_MEDIA;
        s_writer.generation = fat32_mount_generation();
        s_writer.directory_slot = (fat_dir_slot_t){0};
        s_writer.directory_search = (fat_cluster_search_t){0};
        s_writer.state = WRITER_PREPARING;
    }
    if(!same_media())
    {
        s_writer.state = WRITER_IDLE;
        return SCRIPT_SD_NO_MEDIA;
    }
    uint32_t directory = 0U;
    script_sd_result_t result = prepare_directory(&directory);
    if(result == SCRIPT_SD_PENDING)
        return result;
    if(result == SCRIPT_SD_OK)
        result = create_temporary(directory);
    s_writer.state = result == SCRIPT_SD_OK ? WRITER_OPEN : WRITER_IDLE;
    return result;
}

static script_sd_result_t open_writer(void)
{
    if(s_writer.state != WRITER_OPEN)
        return SCRIPT_SD_BUSY;
    if(!same_media())
    {
        script_sd_abort();
        return SCRIPT_SD_NO_MEDIA;
    }
    return SCRIPT_SD_OK;
}

script_sd_result_t script_sd_write(const uint8_t *data, size_t length, size_t *accepted)
{
    *accepted = 0U;
    script_sd_result_t status = open_writer();
    if(status != SCRIPT_SD_OK)
        return status;
    hk_result_t result = fat_append_write(&s_writer.file, data, length, accepted);
    if(result < 0)
        script_sd_abort();
    return storage_result(result);
}

script_sd_result_t script_sd_write_run(uint32_t remaining, sd_block_source_t source,
                                       void *context, size_t *accepted)
{
    *accepted = 0U;
    script_sd_result_t status = open_writer();
    if(status != SCRIPT_SD_OK)
        return status;
    hk_result_t result = fat_append_write_run(&s_writer.file, remaining, source, context, accepted);
    if(result < 0)
        script_sd_abort();
    return storage_result(result);
}

script_sd_result_t script_sd_finish(void)
{
    script_sd_result_t status = open_writer();
    if(status != SCRIPT_SD_OK)
        return status;
    hk_result_t result = fat_append_flush(&s_writer.file);
    if(result == HK_OK)
    {
        uint8_t short_name[11];
        fat_make_short_name(short_name, s_writer.base, s_writer.extension);
        result = fat_append_publish(&s_writer.file, (const char *)short_name + 8U, NULL, 0U);
    }
    if(result == HK_OK)
        s_writer.state = WRITER_IDLE;
    else if(result < 0)
        script_sd_abort();
    return storage_result(result);
}
