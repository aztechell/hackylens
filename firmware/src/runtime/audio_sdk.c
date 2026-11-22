#include <hackylens/app/audio.h>

#include "app_services.h"
#include "../services/audio_service.h"

hk_result_t app_audio_retire(void)
{
    return audio_service_cancel(HK_AUDIO_OWNER_APP);
}

hk_result_t hk_app_audio_info(const hk_app_context_t *ctx, hk_app_audio_info_t *info)
{
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_app_audio_info_t){0};
    hk_result_t result = app_service_context(ctx, 0U);
    if(result != HK_OK)
        return result;
    if(!audio_service_available())
        return HK_ERR_FEATURE_UNAVAILABLE;
    *info = (hk_app_audio_info_t){HK_AUDIO_RATE_HZ, HK_AUDIO_PCM_MAX_BYTES};
    return HK_OK;
}

hk_result_t hk_app_audio_play_begin(const hk_app_context_t *ctx, const uint8_t *pcm,
                                    size_t size)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ?
        audio_service_begin(HK_AUDIO_OWNER_APP, HK_AUDIO_PLAY, (volatile uint8_t *)pcm, size) :
        result;
}

hk_result_t hk_app_audio_record_begin(const hk_app_context_t *ctx, uint8_t *pcm,
                                      size_t size)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ? audio_service_begin(HK_AUDIO_OWNER_APP, HK_AUDIO_RECORD, pcm, size) :
        result;
}

hk_result_t hk_app_audio_poll(const hk_app_context_t *ctx)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ? audio_service_poll(HK_AUDIO_OWNER_APP) : result;
}

hk_result_t hk_app_audio_cancel(const hk_app_context_t *ctx)
{
    hk_result_t result = app_service_context(ctx, 0U);
    return result == HK_OK ? app_audio_retire() : result;
}
