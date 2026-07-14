#include "image_decode.h"

#include <string.h>

#include "../config/fat32_config.h"
#include "image_viewer.h"
#include "../core/hk_string.h"

static uint8_t files_name_is_bmp(const char *name)
{
    size_t len = strlen(name);
    if(len < 4)
        return 0;
    return name[len - 4] == '.' &&
           ascii_lower(name[len - 3]) == 'b' &&
           ascii_lower(name[len - 2]) == 'm' &&
           ascii_lower(name[len - 1]) == 'p';
}

static uint8_t files_name_has_ext(const char *name, const char *ext)
{
    size_t len = strlen(name);
    if(len < 4 || name[len - 4] != '.')
        return 0;
    return ascii_lower(name[len - 3]) == ascii_lower(ext[0]) &&
           ascii_lower(name[len - 2]) == ascii_lower(ext[1]) &&
           ascii_lower(name[len - 1]) == ascii_lower(ext[2]);
}

uint8_t files_entry_is_image(const fat_file_entry_t *entry)
{
    if(entry->attr & FAT_ATTR_DIR)
        return 0;
    return files_name_is_bmp(entry->name) ||
           files_name_has_ext(entry->name, "RAW") ||
           files_name_has_ext(entry->name, "PPM") ||
           files_name_has_ext(entry->name, "PNG");
}

file_result_t files_open_image_entry(const fat_file_entry_t *entry, const file_image_sink_t *sink)
{
    if(files_name_is_bmp(entry->name))
        return files_open_bmp(entry, sink);
    if(files_name_has_ext(entry->name, "RAW"))
        return files_open_raw565(entry, sink);
    if(files_name_has_ext(entry->name, "PPM"))
        return files_open_ppm(entry, sink);
    if(files_name_has_ext(entry->name, "PNG"))
        return files_open_png(entry, sink);
    return FILE_RESULT_UNSUPPORTED_FORMAT;
}
