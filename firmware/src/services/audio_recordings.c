#include "board_config.h"

#if HK_BOARD_AUDIO_ES8374
#include "audio_recordings.h"

#include <stdio.h>
#include <string.h>
#include <hackylens/capability/time.h>

#include "audio_service.h"
#include "wav_pcm.h"
#include "../config/fat32_config.h"
#include "../core/hk_string.h"
#include "../storage/fat32_append.h"
#include "../storage/fat32_directory.h"
#include "../storage/fat32_volume.h"
#include "../storage/file_dir_scan.h"
#include "../storage/file_mount.h"

#define RECORD_DIR "hackyens.records"
#define RECORD_MAX_FILES 64U
#define RECORD_MAX_INDEX 99999U

static fat_file_entry_t s_entries[RECORD_MAX_FILES];
static uint8_t s_count;
static struct
{
    fat_append_t file;
    uint8_t pcm[HK_AUDIO_BLOCK_BYTES];
    uint16_t offset;
    const char *status;
    const char *warning;
    uint64_t blocked_until;
    uint32_t frames;
    uint8_t active;
    uint8_t stopping;
    uint8_t dma_done;
    uint8_t preparing;
    char saved[28];
} s_record;

typedef struct
{
    uint32_t indices[2U * RECORD_MAX_FILES];
    uint32_t maximum;
    uint16_t count;
    uint8_t used;
} catalog_t;

typedef struct
{
    uint32_t current;
    uint16_t aliases;
    uint8_t name_taken;
} recording_dirs_t;

static uint32_t recording_index(const uint8_t *name)
{
    if(ascii_lower(name[0]) != 'r' || ascii_lower(name[1]) != 'e' ||
       ascii_lower(name[2]) != 'c')
        return 0U;
    uint32_t index = 0U;
    for(uint8_t i = 3U; i < 8U; i++)
    {
        if(name[i] < '0' || name[i] > '9')
            return 0U;
        index = index * 10U + name[i] - '0';
    }
    return index;
}

static void reserve_name(catalog_t *catalog, uint32_t index)
{
    if(!index)
        return;
    catalog->indices[catalog->used++] = index;
    if(index > catalog->maximum)
        catalog->maximum = index;
}

static uint8_t read_catalog(const fat_file_entry_t *entry,
                            const uint8_t short_name[11], void *context)
{
    catalog_t *catalog = context;
    if(++catalog->count > RECORD_MAX_FILES)
        return 1U;
    reserve_name(catalog, recording_index(short_name));
    uint32_t index = strlen(entry->name) == 12U ?
        recording_index((const uint8_t *)entry->name) : 0U;
    reserve_name(catalog, index);
    if(index && !(entry->attr & FAT_ATTR_DIR) && str_eq_ci(entry->name + 8U, ".WAV"))
    {
        uint8_t slot = s_count++;
        while(slot &&
              recording_index((const uint8_t *)s_entries[slot - 1U].name) < index)
        {
            s_entries[slot] = s_entries[slot - 1U];
            slot--;
        }
        s_entries[slot] = *entry;
    }
    return 0U;
}

static uint8_t read_recording_dirs(const fat_file_entry_t *entry,
                                   const uint8_t short_name[11], void *context)
{
    recording_dirs_t *dirs = context;
    uint8_t directory = (entry->attr & FAT_ATTR_DIR) &&
        entry->cluster >= 2U && entry->cluster < FAT32_EOC;
    if(str_eq_ci(entry->name, RECORD_DIR))
    {
        dirs->name_taken = 1U;
        if(directory)
            dirs->current = entry->cluster;
    }
    if(!memcmp(short_name, "HACKY~", 6U) && !memcmp(short_name + 7U, " REC", 4U) &&
       short_name[6] >= '1' && short_name[6] <= '9')
        dirs->aliases |= (uint16_t)(1U << (short_name[6] - '1'));
    return 0U;
}

static uint8_t create_recording_dir(recording_dirs_t *dirs)
{
    char alias[] = "HACKY~1";
    uint8_t entries[3U * 32U], name[11];
    fat_dir_slot_t slot;
    uint32_t root = hk_fat_root_cluster();
    uint32_t cluster;
    uint8_t index = 0U;
    while(index < 9U && (dirs->aliases & (1U << index)))
        index++;
    if(index == 9U || dirs->name_taken || !fat_dir_find_slots(root, 3U, &slot) ||
       !fat_alloc_chain(1U, &cluster))
        return 0U;
    if(!fat_init_directory_cluster(cluster, root))
    {
        (void)fat_free_chain(cluster);
        return 0U;
    }

    alias[6] += index;
    fat_make_short_name(name, alias, "REC");
    uint8_t checksum = fat_lfn_checksum(name);
    fat_make_lfn_entry(entries, RECORD_DIR, 2U, 1U, checksum);
    fat_make_lfn_entry(entries + 32U, RECORD_DIR, 1U, 0U, checksum);
    fat_make_short_entry(entries + 64U, name, FAT_ATTR_DIR, cluster, 0U);
    if(!fat_dir_write_slots(&slot, entries, 3U))
        return 0U;
    dirs->current = cluster;
    return 1U;
}

static uint8_t index_used(const catalog_t *catalog, uint32_t index)
{
    for(uint8_t i = 0U; i < catalog->used; i++)
        if(catalog->indices[i] == index)
            return 1U;
    return 0U;
}

const char *audio_recordings_load(void)
{
    recording_dirs_t dirs = {0};
    catalog_t catalog = {0};
    s_count = 0U;
    if(!file_mount_if_needed())
        return "NO SD CARD";
    if(!file_dir_scan(hk_fat_root_cluster(), read_recording_dirs, &dirs) ||
       (dirs.current && !file_dir_scan(dirs.current, read_catalog, &catalog)))
        return "SD READ ERROR";
    return NULL;
}

uint8_t audio_recordings_count(void)
{
    return s_count;
}

const fat_file_entry_t *audio_recordings_get(uint8_t index)
{
    return index < s_count ? &s_entries[index] : NULL;
}

void audio_recording_begin(void)
{
    recording_dirs_t dirs = {0};
    uint32_t index;
    catalog_t catalog = {0};
    char base[9];
    uint8_t header[HK_WAV_HEADER_BYTES];
    size_t written;

    if(s_record.active)
        return;
    if(!file_mount_if_needed())
    {
        s_record.status = "NO SD CARD";
        return;
    }
    s_count = 0U;
    if(!file_dir_scan(hk_fat_root_cluster(), read_recording_dirs, &dirs) ||
       (!dirs.current && !create_recording_dir(&dirs)) ||
       !file_dir_scan(dirs.current, read_catalog, &catalog))
    {
        s_record.status = "RECORDS SD ERROR";
        return;
    }
    if(catalog.count >= RECORD_MAX_FILES)
    {
        s_record.status = "RECORDS FULL";
        return;
    }
    index = catalog.maximum + 1U;
    if(index > RECORD_MAX_INDEX)
    {
        index = 1U;
        while(index_used(&catalog, index))
            index++;
    }
    (void)snprintf(base, sizeof(base), "REC%05u", (unsigned)index);
    if(fat_append_begin(&s_record.file, dirs.current, base) != HK_OK)
    {
        s_record.status = "SD WRITE ERROR";
        return;
    }
    wav_pcm_header(header, HK_AUDIO_RATE_HZ, 0U);
    (void)fat_append_write(&s_record.file, header, sizeof(header), &written);
    (void)snprintf(s_record.saved, sizeof(s_record.saved), "SAVED %s.WAV", base);
    s_record.frames = 0U;
    s_record.offset = sizeof(s_record.pcm);
    s_record.active = 1U;
    s_record.stopping = 0U;
    s_record.dma_done = 0U;
    s_record.preparing = 1U;
    s_record.warning = NULL;
    s_record.blocked_until = 0U;
    s_record.status = "PREPARING SD";
}

void audio_recording_end(void)
{
    if(!s_record.active || s_record.stopping)
        return;
    s_record.stopping = 1U;
    s_record.status = "SAVING";
    if(!s_record.preparing)
        audio_stream_end(HK_AUDIO_OWNER_RECORDER);
}

static void publish_recording(void)
{
    uint8_t header[HK_WAV_HEADER_BYTES];
    uint32_t bytes = s_record.file.committed;
    if(bytes <= HK_WAV_HEADER_BYTES)
        s_record.status = fat_append_discard_empty(&s_record.file) == HK_OK ?
            (s_record.warning ? "RECORDING FAILED" : "NO AUDIO CAPTURED") : "SD WRITE ERROR";
    else
    {
        wav_pcm_header(header, HK_AUDIO_RATE_HZ, bytes - HK_WAV_HEADER_BYTES);
        s_record.status = fat_append_publish(&s_record.file, "WAV", header,
                            sizeof(header)) == HK_OK ?
            (s_record.warning ? s_record.warning : s_record.saved) : "SD WRITE ERROR";
    }
    s_record.active = 0U;
    s_record.preparing = 0U;
}

void audio_recording_close(uint8_t media_lost)
{
    if(!s_record.active)
        return;
    audio_stream_cancel(HK_AUDIO_OWNER_RECORDER);
    if(media_lost || !hk_fat_mounted())
    {
        s_record.status = "SD CARD REMOVED";
        s_record.active = 0U;
        s_record.preparing = 0U;
    }
    else
        publish_recording();
}

static uint8_t card_stalled(void)
{
    uint64_t now;
    if(hk_time_now_us(hk_time_service(), &now) != HK_OK)
        return 1U;
    if(!s_record.blocked_until)
        s_record.blocked_until = now + 350000U;
    return now >= s_record.blocked_until;
}

static void prepare_recording(void)
{
    hk_result_t result = HK_PENDING;
    if(!s_record.stopping)
    {
        for(uint8_t budget = 0U; budget < 4U && result == HK_PENDING; budget++)
            result = fat_append_prepare(&s_record.file);
        if(result == HK_PENDING)
            return;
        if(result == HK_OK)
        {
            result = audio_stream_begin(HK_AUDIO_OWNER_RECORDER, HK_AUDIO_RECORD);
            if(result == HK_PENDING)
            {
                s_record.preparing = 0U;
                s_record.status = "RECORDING";
                return;
            }
            s_record.status = result == HK_ERR_BUSY ? "AUDIO BUSY" : "AUDIO ERROR";
        }
        else
            s_record.status = result == HK_ERR_LIMIT ? "SD FULL" : "SD PREPARE ERROR";
    }
    else
        s_record.status = "CANCELLED";
    if(fat_append_discard_empty(&s_record.file) != HK_OK)
        s_record.status = "SD WRITE ERROR";
    s_record.active = 0U;
    s_record.preparing = 0U;
}

void audio_recording_tick(void)
{
    const hk_audio_owner_t owner = HK_AUDIO_OWNER_RECORDER;
    if(!s_record.active)
        return;
    if(!hk_fat_mounted())
    {
        audio_recording_close(1U);
        return;
    }
    if(s_record.preparing)
    {
        prepare_recording();
        return;
    }
    for(uint8_t budget = 0U; budget < 2U; budget++)
    {
        if(s_record.offset == sizeof(s_record.pcm))
        {
            if(s_record.dma_done || audio_stream_take(owner, s_record.pcm) != HK_OK)
                break;
            s_record.offset = 0U;
        }
        size_t accepted = 0U;
        hk_result_t result = fat_append_write(&s_record.file, s_record.pcm + s_record.offset,
                             sizeof(s_record.pcm) - s_record.offset, &accepted);
        s_record.offset += (uint16_t)accepted;
        if(result == HK_PENDING && !card_stalled())
            break;
        if(result == HK_ERR_LIMIT || result == HK_PENDING)
        {
            s_record.warning = result == HK_ERR_LIMIT ?
                "SD FULL - PARTIAL SAVED" : "SD TOO SLOW - PARTIAL SAVED";
            audio_recording_close(0U);
            return;
        }
        s_record.blocked_until = 0U;
        if(result != HK_OK)
        {
            s_record.warning = "SD ERROR - PARTIAL SAVED";
            audio_recording_close(0U);
            return;
        }
    }
    if(!s_record.dma_done)
    {
        s_record.frames = audio_stream_frames(owner);
        hk_result_t result = audio_stream_poll(owner);
        if(result != HK_PENDING)
        {
            s_record.dma_done = 1U;
            s_record.stopping = 1U;
            switch(result)
            {
            case HK_OK: break;
            case HK_ERR_LIMIT: s_record.warning = "SD TOO SLOW - PARTIAL SAVED"; break;
            case HK_ERR_OVERFLOW: s_record.warning = "MIC OVERRUN - PARTIAL SAVED"; break;
            default: s_record.warning = "AUDIO ERROR - PARTIAL SAVED"; break;
            }
        }
    }
    if(!s_record.dma_done && !s_record.blocked_until)
    {
        for(uint8_t budget = 0U; budget < 3U; budget++)
            if(fat_append_prefetch(&s_record.file) != HK_PENDING)
                break;
    }
    if(s_record.dma_done && s_record.offset == sizeof(s_record.pcm))
    {
        if(!s_record.file.committed && s_record.file.buffered == HK_WAV_HEADER_BYTES)
        {
            publish_recording();
            return;
        }
        hk_result_t result = fat_append_flush(&s_record.file);
        if(result == HK_OK || result == HK_ERR_LIMIT || (result == HK_PENDING && card_stalled()))
        {
            if(result == HK_ERR_LIMIT)
                s_record.warning = "SD FULL - PARTIAL SAVED";
            else if(result == HK_PENDING)
                s_record.warning = "SD TOO SLOW - PARTIAL SAVED";
            publish_recording();
        }
        else if(result != HK_PENDING)
        {
            s_record.warning = "SD ERROR - PARTIAL SAVED";
            publish_recording();
        }
    }
}

audio_recording_view_t audio_recording_view(void)
{
    return (audio_recording_view_t){
        s_record.status, s_record.frames / HK_AUDIO_RATE_HZ,
        s_record.active, s_record.stopping, s_record.preparing,
    };
}
#endif
