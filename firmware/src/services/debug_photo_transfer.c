#include "debug_photo_transfer.h"

#include <stdio.h>
#include <string.h>

#include "../config/fat32_config.h"
#include "../config/photo_config.h"
#include "../config/sd_config.h"
#include "../core/hk_binary.h"
#include "../core/hk_string.h"
#include "../storage/fat32_stream.h"
#include "../storage/fat32_volume.h"
#include "../storage/file_dir_scan.h"
#include "../storage/file_mount.h"
#include "debug_console_service.h"

#define PHOTO_TRANSFER_CHUNK (2U * SD_BLOCK_SIZE)

static fat_file_entry_t g_file;
static fat_stream_t g_stream;
static uint32_t g_mount_generation;
static uint32_t g_chunk_size;
static uint8_t g_open;
static uint8_t g_sector[SD_BLOCK_SIZE];
static uint8_t g_chunk[PHOTO_TRANSFER_CHUNK];

static uint8_t is_photo_name(const char *name)
{
    if(strlen(name) != 12U || ascii_lower(name[0]) != 'i' ||
       ascii_lower(name[1]) != 'm' || ascii_lower(name[2]) != 'g' || name[8] != '.')
        return 0U;
    for(unsigned i = 3U; i < 8U; i++)
        if(name[i] < '0' || name[i] > '9')
            return 0U;
    return str_eq_ci(name + 9U, "BMP") || str_eq_ci(name + 9U, "RAW") ||
           str_eq_ci(name + 9U, "PPM");
}

/* List entries or stop on an exact photo name when opening a file. */
static uint8_t visit_photo(const fat_file_entry_t *entry,
                           const uint8_t short_name[11], void *context)
{
    (void)short_name;
    if((entry->attr & FAT_ATTR_DIR) || !is_photo_name(entry->name))
        return 0U;
    const char *name = context;
    if(!name)
    {
        char line[48];
        snprintf(line, sizeof(line), "HKPHOTOS ITEM %.12s %lu\n",
                 entry->name, (unsigned long)entry->size);
        debug_console_write_text(line);
        return 0U;
    }
    if(!str_eq_ci(entry->name, name))
        return 0U;
    g_file = *entry;
    return 1U;
}

static void open_or_list(const char *name)
{
    g_open = 0U;
    g_chunk_size = 0U;
    if(name && !is_photo_name(name))
    {
        debug_console_write_text("HKPHOTO ERR NAME\n");
        return;
    }
    if(!file_mount_if_needed())
    {
        debug_console_write_text("HKPHOTO ERR NO FAT32 CARD\n");
        return;
    }
    uint32_t directory = 0U;
    file_dir_scan_result_t found = file_dir_find_directory(
        hk_fat_root_cluster(), PHOTO_DIR_LONG_NAME, PHOTO_DIR_SHORT_NAME, &directory);
    g_file.name[0] = '\0';
    if(found == FILE_DIR_SCAN_ERROR ||
       (found == FILE_DIR_SCAN_FOUND && !file_dir_scan(directory, visit_photo, (void *)name)))
    {
        debug_console_write_text("HKPHOTO ERR DIRECTORY READ\n");
        return;
    }
    if(!name)
    {
        debug_console_write_text("HKPHOTOS END\n");
        return;
    }
    if(!g_file.name[0])
    {
        debug_console_write_text("HKPHOTO ERR NOT FOUND\n");
        return;
    }
    if(!g_file.size || !fat_stream_open(&g_stream, &g_file, 0U))
    {
        debug_console_write_text("HKPHOTO ERR FILE READ\n");
        return;
    }
    g_mount_generation = fat32_mount_generation();
    g_open = 1U;
    char line[48];
    snprintf(line, sizeof(line), "HKPHOTO BEGIN %.12s %lu\n",
             g_file.name, (unsigned long)g_file.size);
    debug_console_write_text(line);
}

static void read_chunk(const char *offset_text)
{
    if(!g_open)
    {
        debug_console_write_text("HKPHOTO ERR NO OPEN FILE\n");
        return;
    }
    if(!hk_sd_present() || !hk_fat_mounted() ||
       g_mount_generation != fat32_mount_generation())
    {
        g_open = 0U;
        debug_console_write_text("HKPHOTO ERR CARD CHANGED\n");
        return;
    }
    uint32_t offset = 0U;
    for(; *offset_text >= '0' && *offset_text <= '9'; offset_text++)
    {
        uint32_t digit = (uint32_t)(*offset_text - '0');
        if(offset > (UINT32_MAX - digit) / 10U)
            break;
        offset = offset * 10U + digit;
    }
    if(*offset_text || offset >= g_file.size ||
       (offset != g_stream.file_offset &&
        (!g_chunk_size || offset != g_stream.file_offset - g_chunk_size)))
    {
        debug_console_write_text("HKPHOTO ERR OFFSET\n");
        return;
    }
    if(offset == g_stream.file_offset)
    {
        uint32_t count = g_file.size - offset;
        if(count > sizeof(g_chunk))
            count = sizeof(g_chunk);
        if(!fat_stream_read_cached(&g_stream, g_chunk, count, g_sector))
        {
            g_open = 0U;
            debug_console_write_text("HKPHOTO ERR FILE READ\n");
            return;
        }
        g_chunk_size = count;
    }
    char line[64];
    snprintf(line, sizeof(line), "HKPHOTO DATA %lu %lu %08lX\n",
             (unsigned long)offset, (unsigned long)g_chunk_size,
             (unsigned long)crc32_update(0U, g_chunk, g_chunk_size));
    /* Keep logs from another core out of the binary payload. */
    debug_console_set_framed_mode(1U);
    debug_console_write_wire((const uint8_t *)line, strlen(line));
    debug_console_write_wire(g_chunk, g_chunk_size);
    debug_console_set_framed_mode(0U);
}

uint8_t debug_photo_handle_command(const char *command)
{
    if(str_eq_ci(command, "HKPHOTOS"))
        open_or_list(NULL);
    else if(strncmp(command, "HKPHOTOREAD ", 12U) == 0 && command[12])
        read_chunk(command + 12U);
    else if(strncmp(command, "HKPHOTO ", 8U) == 0)
        open_or_list(command + 8U);
    else
        return 0U;
    return 1U;
}
