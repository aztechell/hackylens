#include "board_config.h"

#if HK_BOARD_AUDIO_ES8374
#include "audio_player.h"
#include "wav_pcm.h"

#include "../config/sd_config.h"
#include "../core/hk_binary.h"
#include "../storage/fat32_file.h"
#include "../storage/fat32_stream.h"
#include "../storage/fat32_volume.h"

#define PLAYER_PRIME_BLOCKS 4U
#define PLAYER_OUTPUT_BUDGET 384U

static struct
{
    fat_file_entry_t file;
    fat_stream_t stream;
    wav_pcm_t format;
    uint32_t total_frames;
    uint32_t source_frame;
    uint32_t position;
    uint32_t phase;
    int32_t left;
    int32_t right;
    const char *status;
    hk_audio_owner_t owner;
    uint16_t buffered;
    uint8_t playing;
    uint8_t primed;
    uint8_t submitted;
    uint8_t final_block;
    uint8_t sector[SD_BLOCK_SIZE];
    uint8_t pcm[HK_AUDIO_BLOCK_BYTES];
} s_player;

static uint8_t read_at(void *context, uint32_t offset, uint8_t *dst, uint32_t bytes)
{
    return fat_file_read_at(context, offset, dst, bytes);
}

const char *audio_volume_adjust(int8_t steps)
{
    int16_t volume = audio_service_volume() + steps * 10;
    if(volume < 0)
        volume = 0;
    if(volume > 100)
        volume = 100;
    return audio_service_set_volume((uint8_t)volume) == HK_OK ? NULL : "VOLUME ERROR";
}

const char *audio_player_open(hk_audio_owner_t owner, const fat_file_entry_t *entry)
{
    wav_pcm_t format;
    if(s_player.owner)
        return "AUDIO BUSY";
    if(!hk_fat_mounted())
        return "NO SD CARD";
    switch(wav_pcm_read(read_at, (void *)entry, entry->size, &format))
    {
    case HK_WAV_IO: return "SD READ ERROR";
    case HK_WAV_INVALID: return "INVALID WAV";
    case HK_WAV_UNSUPPORTED: return "UNSUPPORTED WAV";
    case HK_WAV_OK: break;
    }
    s_player.owner = owner;
    s_player.file = *entry;
    s_player.format = format;
    s_player.total_frames = format.bytes / (2U * format.channels);
    s_player.position = 0U;
    s_player.status = "READY";
    return NULL;
}

static uint8_t read_sample(int32_t *sample)
{
    uint8_t frame[4];
    if(!fat_stream_read_cached(&s_player.stream, frame,
                               2U * s_player.format.channels, s_player.sector))
        return 0U;
    *sample = wav_pcm_sample(frame, s_player.format.channels);
    return 1U;
}

void audio_player_toggle(hk_audio_owner_t owner)
{
    if(s_player.owner != owner)
        return;
    if(s_player.playing)
    {
        audio_stream_cancel(owner);
        s_player.playing = 0U;
        s_player.position = 0U;
        s_player.status = "STOPPED";
        return;
    }
    if(!hk_fat_mounted() ||
       !fat_stream_open(&s_player.stream, &s_player.file, s_player.format.offset) ||
       !read_sample(&s_player.left))
    {
        s_player.status = "SD READ ERROR";
        return;
    }
    s_player.right = s_player.left;
    if(s_player.total_frames > 1U && !read_sample(&s_player.right))
    {
        s_player.status = "SD READ ERROR";
        return;
    }
    hk_result_t result = audio_stream_begin(owner, HK_AUDIO_PLAY);
    if(result != HK_PENDING)
    {
        s_player.status = result == HK_ERR_BUSY ? "AUDIO BUSY" : "AUDIO ERROR";
        return;
    }
    s_player.playing = 1U;
    s_player.position = 0U;
    s_player.buffered = 0U;
    s_player.source_frame = 0U;
    s_player.phase = 0U;
    s_player.primed = 0U;
    s_player.submitted = 0U;
    s_player.final_block = 0U;
    s_player.status = "BUFFERING";
}

void audio_player_close(hk_audio_owner_t owner)
{
    if(s_player.owner != owner)
        return;
    if(s_player.playing)
        audio_stream_cancel(owner);
    s_player.playing = 0U;
    s_player.owner = 0U;
}

audio_player_view_t audio_player_view(hk_audio_owner_t owner)
{
    if(s_player.owner != owner)
        return (audio_player_view_t){0};
    return (audio_player_view_t){
        s_player.file.name, s_player.status, s_player.position,
        s_player.total_frames / s_player.format.rate, s_player.playing,
    };
}

static uint8_t fill_output(void)
{
    /* Limit work per idle tick, including at 48 kHz stereo. */
    for(uint16_t budget = 0; budget < PLAYER_OUTPUT_BUDGET &&
        s_player.buffered < sizeof(s_player.pcm) &&
        s_player.source_frame < s_player.total_frames; budget++)
    {
        int32_t sample = (s_player.left * (int32_t)(HK_AUDIO_RATE_HZ - s_player.phase) +
                          s_player.right * (int32_t)s_player.phase) / (int32_t)HK_AUDIO_RATE_HZ;
        wr16(s_player.pcm + s_player.buffered, (uint16_t)sample);
        s_player.buffered += 2U;
        s_player.phase += s_player.format.rate;
        while(s_player.phase >= HK_AUDIO_RATE_HZ &&
              s_player.source_frame < s_player.total_frames)
        {
            s_player.phase -= HK_AUDIO_RATE_HZ;
            s_player.source_frame++;
            s_player.left = s_player.right;
            if(s_player.source_frame + 1U < s_player.total_frames &&
               !read_sample(&s_player.right))
                return 0U;
        }
    }
    return 1U;
}

static void player_error(const char *message)
{
    audio_stream_cancel(s_player.owner);
    s_player.playing = 0U;
    s_player.status = message;
}

uint8_t audio_player_active(void)
{
    return s_player.playing;
}

void audio_player_tick(void)
{
    if(!s_player.playing)
        return;
    if(!hk_fat_mounted() || (!s_player.final_block && !fill_output()))
    {
        player_error("SD READ ERROR");
        return;
    }
    if(!s_player.final_block &&
       (s_player.buffered == sizeof(s_player.pcm) ||
        s_player.source_frame == s_player.total_frames))
    {
        uint8_t last = s_player.source_frame == s_player.total_frames;
        hk_result_t result = audio_stream_push(s_player.owner, s_player.pcm,
                                              s_player.buffered, last);
        if(result == HK_OK)
        {
            s_player.buffered = 0U;
            s_player.final_block = last;
            if(s_player.submitted < PLAYER_PRIME_BLOCKS)
                s_player.submitted++;
            s_player.primed = s_player.submitted >= PLAYER_PRIME_BLOCKS || last;
        }
        else if(result != HK_PENDING)
        {
            player_error(result == HK_ERR_LIMIT ? "AUDIO UNDERRUN" : "AUDIO ERROR");
            return;
        }
    }
    if(!s_player.primed)
        return;
    s_player.position = audio_stream_frames(s_player.owner) / HK_AUDIO_RATE_HZ;
    hk_result_t result = audio_stream_poll(s_player.owner);
    if(result == HK_PENDING)
        s_player.status = "PLAYING";
    else if(result == HK_OK)
    {
        s_player.playing = 0U;
        s_player.position = s_player.total_frames / s_player.format.rate;
        s_player.status = "DONE";
    }
    else
        player_error(result == HK_ERR_LIMIT ? "AUDIO UNDERRUN" : "AUDIO ERROR");
}
#endif
