#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "audio_player.h"
#include "fat32_append.h"
#include "fat32_directory.h"
#include "fat32_file.h"
#include "fat32_stream.h"
#include "hal_audio.h"
#include "hk_binary.h"
#include "wav_pcm.h"

static uint8_t disk[8][512];
static uint8_t directory[32];
static uint8_t wav[1200];
static uint8_t mounted = 1U;
static uint8_t disk_full;
static uint8_t volume = 100U;
static uint8_t hw_volume;
static uint32_t allocation;
static uint32_t source_size;
static uint32_t output_frames;
static uint32_t cancel_count;
static uint8_t last_block;
static int16_t first_output;

uint8_t hk_fat_mounted(void) { return mounted; }
uint8_t fat32_sectors_per_cluster(void) { return 2U; }
uint32_t fat_cluster_lba(uint32_t cluster) { return 2U * (cluster - 2U); }

uint8_t sd_card_read_block(uint32_t lba, uint8_t *dst)
{
    assert(lba < 8U);
    memcpy(dst, disk[lba], 512U);
    return mounted;
}

uint8_t sd_card_write_block_fast(uint32_t lba, const uint8_t *src)
{
    assert(lba < 8U);
    if(!mounted)
        return 0U;
    memcpy(disk[lba], src, 512U);
    return 1U;
}

uint8_t sd_card_write_block(uint32_t lba, const uint8_t *src)
{
    return sd_card_write_block_fast(lba, src);
}

uint8_t sd_card_write_run(uint32_t lba, uint16_t count,
                          sd_block_source_t source, void *context)
{
    for(uint16_t i = 0U; i < count; i++)
        if(!sd_card_write_block_fast(lba + i, source(context, i)))
            return 0U;
    return 1U;
}

uint8_t fat_write_entry(uint32_t cluster, uint32_t value)
{
    (void)cluster;
    (void)value;
    return mounted;
}

uint32_t fat_next_cluster(uint32_t cluster)
{
    (void)cluster;
    return 0U;
}

uint8_t fat_read_next_cluster(uint32_t cluster, uint32_t *next)
{
    *next = fat_next_cluster(cluster);
    return mounted;
}

uint8_t fat_free_chain(uint32_t cluster)
{
    assert(cluster == 2U);
    return 1U;
}

hk_result_t fat_find_free_step(fat_cluster_search_t *search, uint32_t *cluster)
{
    if(!search->next++)
        return HK_PENDING;
    if(disk_full)
        return HK_ERR_LIMIT;
    *cluster = 2U + allocation++;
    return HK_OK;
}

uint8_t fat_dir_find_slots(uint32_t cluster, uint8_t count, fat_dir_slot_t *slot)
{
    assert(cluster == 12U && count == 1U);
    *slot = (fat_dir_slot_t){12U, 0U, 0U};
    return mounted;
}

uint8_t fat_dir_write_slots(const fat_dir_slot_t *slot, const uint8_t *data, uint8_t count)
{
    assert(slot->cluster == 12U && count == 1U);
    memcpy(directory, data, sizeof(directory));
    return mounted;
}

void fat_make_short_name(uint8_t name[11], const char *base, const char *ext)
{
    memcpy(name, base, 8U);
    memcpy(name + 8U, ext, 3U);
}

void fat_make_short_entry(uint8_t *entry, const uint8_t name[11], uint8_t attr,
                          uint32_t cluster, uint32_t bytes)
{
    memset(entry, 0, 32U);
    memcpy(entry, name, 11U);
    entry[11] = attr;
    wr16(entry + 26U, (uint16_t)cluster);
    wr32(entry + 28U, bytes);
}

static uint8_t read_wav(void *context, uint32_t offset, uint8_t *dst, uint32_t bytes)
{
    (void)context;
    if(offset > source_size || bytes > source_size - offset || !mounted)
        return 0U;
    memcpy(dst, wav + offset, bytes);
    return 1U;
}

uint8_t fat_file_read_at(const fat_file_entry_t *entry, uint32_t offset,
                         uint8_t *dst, uint32_t bytes)
{
    assert(entry->size == source_size);
    return read_wav(NULL, offset, dst, bytes);
}

uint8_t fat_stream_open(fat_stream_t *stream, const fat_file_entry_t *entry, uint32_t offset)
{
    stream->entry = entry;
    stream->file_offset = offset;
    return offset < entry->size;
}

uint8_t fat_stream_read_cached(fat_stream_t *stream, uint8_t *dst, uint32_t bytes,
                               uint8_t *sector)
{
    assert(sector != NULL);
    if(!read_wav(NULL, stream->file_offset, dst, bytes))
        return 0U;
    stream->file_offset += bytes;
    return 1U;
}

uint8_t settings_audio_volume(void) { return volume; }
void settings_set_audio_volume(uint8_t percent) { volume = percent; }
void settings_mark_dirty(uint8_t immediate) { assert(!immediate); }

hk_result_t hal_audio_set_volume(uint8_t percent)
{
    hw_volume = percent;
    return HK_OK;
}

hk_result_t hal_audio_begin(hk_audio_direction_t direction, volatile uint8_t *pcm, size_t frames)
{
    assert(direction == HK_AUDIO_PLAY && pcm && frames == 1U);
    return HK_PENDING;
}

hk_result_t hal_audio_stream_begin(hk_audio_direction_t direction)
{
    assert(direction == HK_AUDIO_PLAY);
    output_frames = 0U;
    last_block = 0U;
    return HK_PENDING;
}

hk_result_t hal_audio_stream_push(const uint8_t *pcm, size_t frames, uint8_t last)
{
    if(!output_frames)
        first_output = (int16_t)rd16(pcm);
    output_frames += (uint32_t)frames;
    last_block = last;
    return HK_OK;
}

hk_result_t hal_audio_stream_take(uint8_t *pcm) { (void)pcm; return HK_PENDING; }
void hal_audio_stream_end(void) {}
uint32_t hal_audio_stream_frames(void) { return output_frames; }
hk_result_t hal_audio_poll(void) { return last_block ? HK_OK : HK_PENDING; }
hk_result_t hal_audio_cancel(void) { cancel_count++; return HK_OK; }

static void stereo_wave(uint32_t rate)
{
    const uint32_t frames = 256U;
    source_size = HK_WAV_HEADER_BYTES + frames * 4U;
    wav_pcm_header(wav, rate, frames * 4U);
    wr16(wav + 22U, 2U);
    wr16(wav + 32U, 4U);
    wr32(wav + 28U, rate * 4U);
    for(uint32_t i = HK_WAV_HEADER_BYTES; i < source_size; i += 4U)
    {
        wr16(wav + i, 2000U);
        wr16(wav + i + 2U, (uint16_t)-1000);
    }
}

static void test_parse_and_resample(void)
{
    const uint32_t rates[] = {8000U, 15869U, 22050U, 44100U, 48000U};
    fat_file_entry_t entry = {.name = "EXTERNAL.WAV", .cluster = 2U};
    for(size_t i = 0U; i < sizeof(rates) / sizeof(rates[0]); i++)
    {
        stereo_wave(rates[i]);
        entry.size = source_size;
        assert(audio_player_open(HK_AUDIO_OWNER_FILES, &entry) == NULL);
        audio_player_toggle(HK_AUDIO_OWNER_FILES);
        assert(audio_service_cancel() == HK_OK && cancel_count == 0U);
        uint8_t python_pcm[2] = {0U};
        assert(audio_service_begin(HK_AUDIO_PLAY, python_pcm, 2U) == HK_ERR_BUSY);
        for(uint8_t tick = 0U; tick < 12U; tick++)
            audio_player_tick();
        assert(first_output == 500);
        assert(output_frames == (256U * HK_AUDIO_RATE_HZ + rates[i] - 1U) / rates[i]);
        assert(!audio_player_view(HK_AUDIO_OWNER_FILES).playing);
        audio_player_close(HK_AUDIO_OWNER_FILES);
    }

    wav_pcm_t parsed;
    stereo_wave(48000U);
    wr16(wav + 20U, 3U);
    assert(wav_pcm_read(read_wav, NULL, source_size, &parsed) == HK_WAV_UNSUPPORTED);
    wr16(wav + 20U, 1U);
    wr32(wav + 40U, UINT32_MAX);
    assert(wav_pcm_read(read_wav, NULL, source_size, &parsed) == HK_WAV_INVALID);
    stereo_wave(48000U);
    assert(wav_pcm_read(read_wav, NULL, source_size - 1U, &parsed) == HK_WAV_INVALID);
    assert(audio_service_set_volume(0U) == HK_OK && hw_volume == 0U && volume == 0U);
    uint8_t python_pcm[2] = {0U};
    assert(audio_service_begin(HK_AUDIO_PLAY, python_pcm, 2U) == HK_PENDING);
    audio_stream_cancel(HK_AUDIO_OWNER_FILES);
    assert(cancel_count == 0U);
    assert(audio_service_cancel() == HK_OK && cancel_count == 1U);
}

static void test_append_partial(void)
{
    fat_append_t writer;
    uint8_t source[1400] = {0};
    size_t accepted;
    assert(fat_append_begin(&writer, 12U, "REC00001") == HK_OK);
    assert(!memcmp(directory, "REC00001TMP", 11U));
    assert(fat_append_write(&writer, source, sizeof(source), &accepted) == HK_PENDING);
    assert(accepted == 512U);
    assert(fat_append_write(&writer, source + 512U, 888U, &accepted) == HK_OK);
    assert(accepted == 0U);
    assert(fat_append_write(&writer, source + 512U, 888U, &accepted) == HK_OK);
    assert(accepted == 512U && writer.committed == 1024U);
    assert(fat_append_write(&writer, source + 1024U, 376U, &accepted) == HK_OK);
    assert(accepted == 376U);
    disk_full = 1U;
    assert(fat_append_flush(&writer) == HK_PENDING);
    assert(fat_append_flush(&writer) == HK_ERR_LIMIT);
    uint8_t header[HK_WAV_HEADER_BYTES];
    wav_pcm_header(header, HK_AUDIO_RATE_HZ, writer.committed - HK_WAV_HEADER_BYTES);
    mounted = 0U;
    assert(fat_append_publish(&writer, "WAV", header, sizeof(header)) == HK_ERR_IO);
    assert(!memcmp(directory, "REC00001TMP", 11U));
    mounted = 1U;
    assert(fat_append_publish(&writer, "WAV", header, sizeof(header)) == HK_OK);
    assert(!memcmp(directory, "REC00001WAV", 11U) && rd32(directory + 28U) == 1024U);
    memcpy(wav, disk, 1024U);
    source_size = 1024U;
    wav_pcm_t parsed;
    assert(wav_pcm_read(read_wav, NULL, source_size, &parsed) == HK_WAV_OK);
    assert(parsed.rate == HK_AUDIO_RATE_HZ && parsed.bytes == 980U);
}

static void test_prepare_and_prefetch(void)
{
    fat_append_t writer;
    uint8_t data[512] = {0};
    size_t accepted;
    disk_full = 0U;
    allocation = 0U;
    assert(fat_append_begin(&writer, 12U, "REC00002") == HK_OK);
    assert(fat_append_prepare(&writer) == HK_PENDING);
    assert(writer.first_cluster == 0U && writer.committed == 0U);
    assert(fat_append_prepare(&writer) == HK_OK);
    assert(writer.first_cluster == 2U && writer.committed == 0U);
    assert(fat_append_prefetch(&writer) == HK_PENDING);
    assert(fat_append_prefetch(&writer) == HK_OK);
    assert(writer.next_cluster == 3U && writer.cluster == 2U);
    for(uint8_t sector = 0U; sector < 3U; sector++)
    {
        assert(fat_append_write(&writer, data, sizeof(data), &accepted) == HK_OK);
        assert(accepted == sizeof(data));
    }
    assert(writer.committed == 1536U && writer.cluster == 3U && !writer.next_cluster);
}

int main(void)
{
    test_parse_and_resample();
    test_append_partial();
    test_prepare_and_prefetch();
    puts("AUDIO_FILES_OK");
    return 0;
}
