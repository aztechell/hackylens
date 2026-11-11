#include <assert.h>
#include <string.h>

#include "file_dir_scan.h"
#include "fat32_allocation.h"
#include "hk_binary.h"
#include "internal/fat32_state_private.h"

static uint8_t disk[4][512];
static uint8_t scratch[512];
static uint8_t fail_fat_read;
static uint8_t allow_writes;
static unsigned writes;
static int fail_write_lba = -1;
static fat32_geometry_t geometry = {
    .fat_begin = 0U, .data_begin = 1U, .cluster_count = 2U, .sectors_per_cluster = 1U,
};

const fat32_geometry_t *fat32_geometry(void) { return &geometry; }
uint8_t *fat32_sector_scratch(void) { return scratch; }
uint8_t fat32_sectors_per_cluster(void) { return geometry.sectors_per_cluster; }
void fat32_set_alloc_hint(uint32_t cluster) { geometry.alloc_hint = cluster; }

uint8_t sd_card_read_block(uint32_t lba, uint8_t *dst)
{
    assert(lba < 4U);
    if(!lba && fail_fat_read)
        return 0U;
    memcpy(dst, disk[lba], sizeof(disk[0]));
    return 1U;
}

uint8_t sd_card_write_block(uint32_t lba, const uint8_t *src)
{
    assert(lba < 4U && allow_writes && "directory scan must not write");
    if((int)lba == fail_write_lba)
    {
        fail_write_lba = -1;
        return 0U;
    }
    memcpy(disk[lba], src, sizeof(disk[0]));
    writes++;
    return 1U;
}

static uint8_t *fat_entry(unsigned copy, uint32_t cluster)
{
    return disk[copy * 2U + cluster / 128U] + (cluster % 128U) * 4U;
}

static void test_allocation(void)
{
    uint32_t first;
    geometry = (fat32_geometry_t){
        .fat_count = 2U, .sectors_per_fat = 2U, .cluster_count = 254U, .alloc_hint = 120U,
    };
    memset(disk, 0, sizeof(disk));
    allow_writes = 1U;
    for(unsigned copy = 0U; copy < 2U; copy++)
        wr32(fat_entry(copy, 127U), 0xa0000000U);
    assert(fat_alloc_chain(16U, &first) && first == 120U);
    assert(writes == 4U && geometry.alloc_hint == 136U);
    for(unsigned copy = 0U; copy < 2U; copy++)
    {
        for(uint32_t cluster = 120U; cluster < 135U; cluster++)
            assert((rd32(fat_entry(copy, cluster)) & 0x0fffffffU) == cluster + 1U);
        assert(rd32(fat_entry(copy, 127U)) == 0xa0000080U);
        assert(rd32(fat_entry(copy, 135U)) >= 0x0ffffff8U);
        assert(!rd32(fat_entry(copy, 119U)) && !rd32(fat_entry(copy, 136U)));
        wr32(fat_entry(copy, 137U), 0x0fffffffU);
    }
    assert(fat_alloc_chain(2U, &first) && first == 136U);
    assert(rd32(fat_entry(0U, 136U)) == 138U);
    assert(rd32(fat_entry(1U, 137U)) == 0x0fffffffU);

    fail_write_lba = 3;
    assert(!fat_alloc_chain(2U, &first));
    for(unsigned copy = 0U; copy < 2U; copy++)
        assert(!rd32(fat_entry(copy, 139U)) && !rd32(fat_entry(copy, 140U)));
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
    test_allocation();
    return 0;
}
