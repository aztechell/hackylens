#ifndef HK_HAL_AUDIO_H
#define HK_HAL_AUDIO_H

#include "audio_service.h"

hk_result_t hal_audio_begin(hk_audio_direction_t direction,
                            volatile uint8_t *pcm, size_t frames);
hk_result_t hal_audio_poll(void);
hk_result_t hal_audio_cancel(void);
hk_result_t hal_audio_stream_begin(hk_audio_direction_t direction);
hk_result_t hal_audio_stream_push(const uint8_t *pcm, size_t frames, uint8_t last);
hk_result_t hal_audio_stream_take(uint8_t *pcm);
void hal_audio_stream_end(void);
uint32_t hal_audio_stream_frames(void);
hk_result_t hal_audio_set_volume(uint8_t percent);

#endif
