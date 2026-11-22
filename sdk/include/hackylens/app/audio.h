#ifndef HACKYLENS_APP_AUDIO_H
#define HACKYLENS_APP_AUDIO_H

#include <stddef.h>
#include <hackylens/app/context.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t sample_rate_hz;
    uint32_t max_bytes;
} hk_app_audio_info_t;

/* Signed 16-bit little-endian mono PCM. info returns
 * HK_ERR_FEATURE_UNAVAILABLE on a board without audio. If audio is mandatory,
 * declare audio in app.toml to exclude such boards during composition.
 * size is a positive even byte count no larger than info.max_bytes.
 * A begin that returns HK_PENDING holds the caller's buffer until poll
 * completes, cancel returns, or the runtime retires the app. Use app state
 * or static storage for this buffer. Recording writes it on HK_OK. */
hk_result_t hk_app_audio_info(const hk_app_context_t *ctx, hk_app_audio_info_t *info);
hk_result_t hk_app_audio_play_begin(const hk_app_context_t *ctx, const uint8_t *pcm,
                                    size_t size);
hk_result_t hk_app_audio_record_begin(const hk_app_context_t *ctx, uint8_t *pcm,
                                      size_t size);
hk_result_t hk_app_audio_poll(const hk_app_context_t *ctx);
hk_result_t hk_app_audio_cancel(const hk_app_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
