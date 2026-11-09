#ifndef HK_AUDIO_SERVICE_H
#define HK_AUDIO_SERVICE_H

#include <stddef.h>
#include <stdint.h>
#include <hackylens/capability/common.h>

#define HK_AUDIO_RATE_HZ 15869U
#define HK_AUDIO_PCM_MAX_BYTES 16000U
#define HK_AUDIO_BLOCK_FRAMES 2048U
#define HK_AUDIO_BLOCK_BYTES (2U * HK_AUDIO_BLOCK_FRAMES)

/* Core 0 only. Each transfer uses signed 16-bit little-endian mono PCM.
 * Begin returns HK_PENDING on success. Keep pcm valid until poll finishes
 * or cancel returns. Playback and recording share one hardware channel. */
typedef enum
{
    HK_AUDIO_PLAY,
    HK_AUDIO_RECORD,
} hk_audio_direction_t;

uint8_t audio_service_available(void);
hk_result_t audio_service_begin(hk_audio_direction_t direction,
                                volatile uint8_t *pcm, size_t size);
hk_result_t audio_service_poll(void);
hk_result_t audio_service_cancel(void);

typedef enum
{
    HK_AUDIO_OWNER_FILES = 1,
    HK_AUDIO_OWNER_RECORDER,
} hk_audio_owner_t;

/* Playback submits mono PCM before polling. Capture takes completed blocks.
 * HK_PENDING from push/take means the caller can retry after servicing SD.
 * A recording stop finishes the current DMA block before returning HK_OK. */
hk_result_t audio_stream_begin(hk_audio_owner_t owner, hk_audio_direction_t direction);
hk_result_t audio_stream_push(hk_audio_owner_t owner, const uint8_t *pcm,
                              size_t bytes, uint8_t last);
hk_result_t audio_stream_take(hk_audio_owner_t owner, uint8_t *pcm);
hk_result_t audio_stream_poll(hk_audio_owner_t owner);
uint32_t audio_stream_frames(hk_audio_owner_t owner);
void audio_stream_end(hk_audio_owner_t owner);
void audio_stream_cancel(hk_audio_owner_t owner);
uint8_t audio_service_volume(void);
hk_result_t audio_service_set_volume(uint8_t percent);

#endif
