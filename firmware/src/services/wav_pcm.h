#ifndef HK_WAV_PCM_H
#define HK_WAV_PCM_H

#include <stddef.h>
#include <stdint.h>

#define HK_WAV_HEADER_BYTES 44U

typedef enum
{
    HK_WAV_OK,
    HK_WAV_IO,
    HK_WAV_INVALID,
    HK_WAV_UNSUPPORTED,
} wav_result_t;

typedef struct
{
    uint32_t offset;
    uint32_t bytes;
    uint32_t rate;
    uint16_t channels;
} wav_pcm_t;

typedef uint8_t (*wav_read_at_t)(void *context, uint32_t offset,
                                uint8_t *data, uint32_t bytes);

wav_result_t wav_pcm_read(wav_read_at_t read_at, void *context,
                         uint32_t file_size, wav_pcm_t *pcm);
void wav_pcm_header(uint8_t header[HK_WAV_HEADER_BYTES],
                    uint32_t rate, uint32_t bytes);
int32_t wav_pcm_sample(const uint8_t *frame, uint16_t channels);

#endif
