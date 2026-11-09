#include <assert.h>
#include <string.h>

#include "file_dir_scan.h"
#include "hk_binary.h"
#include "internal/fat32_state_private.h"

static uint8_t disk[3][512];
static uint8_t scratch[512];
static uint8_t fail_fat_read;
static const fat32_geometry_t geometry = {
    .fat_begin = 0U, .data_begin = 1U, .cluster_count = 2U, .sectors_per_cluster = 1U,
};

const fat32_geometry_t *fat32_geometry(void) { return &geometry; }
uint8_t *fat32_sector_scratch(void) { return scratch; }
uint8_t fat32_sectors_per_cluster(void) { return geometry.sectors_per_cluster; }
void fat32_set_alloc_hint(uint32_t cluster) { (void)cluster; }

uint8_t sd_card_read_block(uint32_t lba, uint8_t *dst)
{
    assert(lba < 3U);
    if(!lba && fail_fat_read)
        return 0U;
    memcpy(dst, disk[lba], sizeof(disk[0]));
    return 1U;
}

uint8_t sd_card_write_block(uint32_t lba, const uint8_t *src)
{
    (void)lba;
    (void)src;
    assert(0 && "directory scan must not write");
    return 0U;
}

static uint8_t visit(const fat_file_entry_t *entry, const uint8_t short_name[11], void *context)
{
    assert(strcmp(entry->name, "REC00001.WAV") == 0);
    assert(memcmp(short_name, "REC00001WAV", 11U) == 0);
    ++*(unsigned *)context;
    return 0U;
}

int main(void)
{
    unsigned visits = 0;
    memset(disk[1], 0xe5, sizeof(disk[1]));
    memcpy(disk[2], "REC00001WAV", 11U);
    wr32(disk[0] + 8U, 3U);
    assert(file_dir_scan(2U, visit, &visits) && visits == 1U);

    fail_fat_read = 1U;
    visits = 0U;
    assert(!file_dir_scan(2U, visit, &visits) && visits == 0U);
    fail_fat_read = 0U;

    wr32(disk[0] + 8U, 4U);
    assert(!file_dir_scan(2U, NULL, NULL));
    wr32(disk[0] + 8U, 2U);
    assert(!file_dir_scan(2U, NULL, NULL));
    wr32(disk[0] + 8U, 0x0fffffffU);
    assert(file_dir_scan(2U, visit, &visits) && visits == 0U);
    return 0;
}
