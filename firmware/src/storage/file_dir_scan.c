#include "file_dir_scan.h"

#include "../config/fat32_config.h"

#include "internal/file_browser_state.h"
#include "file_dir.h"
#include "../core/hk_string.h"

file_dir_scan_result_t file_dir_find_directory(uint32_t parent_cluster, const char *long_name, const char *short_name, uint32_t *cluster_out)
{
    if(cluster_out)
        *cluster_out = 0;
    if(!files_load_dir(parent_cluster))
        return FILE_DIR_SCAN_ERROR;

    for(uint8_t i = 0; i < files_count(); i++)
    {
        const fat_file_entry_t *entry = files_entry_at(i);
        if(entry && (entry->attr & FAT_ATTR_DIR) &&
           (str_eq_ci(entry->name, long_name) || str_eq_ci(entry->name, short_name)))
        {
            if(cluster_out)
                *cluster_out = entry->cluster;
            return FILE_DIR_SCAN_FOUND;
        }
    }

    return FILE_DIR_SCAN_NOT_FOUND;
}

uint8_t file_dir_find_max_file_index(uint32_t dir_cluster, file_dir_index_parser_t parser, uint32_t *max_index_out)
{
    uint32_t max_index = 0;

    if(max_index_out)
        *max_index_out = 0;
    if(!files_load_dir(dir_cluster))
        return 0;

    for(uint8_t i = 0; i < files_count(); i++)
    {
        uint32_t index;
        const fat_file_entry_t *entry = files_entry_at(i);
        if(entry && !(entry->attr & FAT_ATTR_DIR) && parser(entry->name, &index) && index > max_index)
            max_index = index;
    }

    if(max_index_out)
        *max_index_out = max_index;
    return 1;
}
