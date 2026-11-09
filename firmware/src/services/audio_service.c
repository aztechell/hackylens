#include "audio_service.h"

#include "board_config.h"
#if HK_BOARD_AUDIO_ES8374
#include "hal_audio.h"
#include "settings_persistence.h"
#include "settings_service.h"

#define AUDIO_OWNER_PYTHON 3U
static uint8_t s_owner;

static hk_result_t audio_poll_owner(uint8_t owner)
{
    hk_result_t result;
    if(s_owner != owner)
        return HK_ERR_INVALID_STATE;
    result = hal_audio_poll();
    if(result != HK_PENDING)
        s_owner = 0U;
    return result;
}

static hk_result_t audio_cancel_owner(uint8_t owner)
{
    if(s_owner != owner)
        return HK_OK;
    s_owner = 0U;
    return hal_audio_cancel();
}
#endif

uint8_t audio_service_available(void)
{
    return HK_BOARD_AUDIO_ES8374;
}

hk_result_t audio_service_begin(hk_audio_direction_t direction,
                                volatile uint8_t *pcm, size_t size)
{
    if((direction != HK_AUDIO_PLAY && direction != HK_AUDIO_RECORD) ||
       !pcm || !size || size % 2U || size > HK_AUDIO_PCM_MAX_BYTES)
        return HK_ERR_INVALID_ARGUMENT;
#if HK_BOARD_AUDIO_ES8374
    if(s_owner)
        return HK_ERR_BUSY;
    hk_result_t result = hal_audio_set_volume(settings_audio_volume());
    if(result == HK_OK)
        result = hal_audio_begin(direction, pcm, size / 2U);
    if(result == HK_PENDING)
        s_owner = AUDIO_OWNER_PYTHON;
    return result;
#else
    return HK_ERR_FEATURE_UNAVAILABLE;
#endif
}

hk_result_t audio_service_poll(void)
{
#if HK_BOARD_AUDIO_ES8374
    return audio_poll_owner(AUDIO_OWNER_PYTHON);
#else
    return HK_ERR_FEATURE_UNAVAILABLE;
#endif
}

hk_result_t audio_service_cancel(void)
{
#if HK_BOARD_AUDIO_ES8374
    return audio_cancel_owner(AUDIO_OWNER_PYTHON);
#else
    return HK_OK;
#endif
}

#if HK_BOARD_AUDIO_ES8374
hk_result_t audio_stream_begin(hk_audio_owner_t owner, hk_audio_direction_t direction)
{
    if((owner != HK_AUDIO_OWNER_FILES && owner != HK_AUDIO_OWNER_RECORDER) ||
       (direction != HK_AUDIO_PLAY && direction != HK_AUDIO_RECORD))
        return HK_ERR_INVALID_ARGUMENT;
    if(s_owner)
        return HK_ERR_BUSY;
    hk_result_t result = hal_audio_set_volume(settings_audio_volume());
    if(result == HK_OK)
        result = hal_audio_stream_begin(direction);
    if(result == HK_PENDING)
        s_owner = (uint8_t)owner;
    return result;
}

hk_result_t audio_stream_push(hk_audio_owner_t owner, const uint8_t *pcm,
                              size_t bytes, uint8_t last)
{
    if(s_owner != owner)
        return HK_ERR_INVALID_STATE;
    if(!pcm || !bytes || bytes % 2U || bytes > HK_AUDIO_BLOCK_BYTES)
        return HK_ERR_INVALID_ARGUMENT;
    return hal_audio_stream_push(pcm, bytes / 2U, last);
}

hk_result_t audio_stream_take(hk_audio_owner_t owner, uint8_t *pcm)
{
    if(s_owner != owner)
        return HK_ERR_INVALID_STATE;
    return pcm ? hal_audio_stream_take(pcm) : HK_ERR_INVALID_ARGUMENT;
}

hk_result_t audio_stream_poll(hk_audio_owner_t owner)
{
    return audio_poll_owner(owner);
}

uint32_t audio_stream_frames(hk_audio_owner_t owner)
{
    return s_owner == owner ? hal_audio_stream_frames() : 0U;
}

void audio_stream_end(hk_audio_owner_t owner)
{
    if(s_owner == owner)
        hal_audio_stream_end();
}

void audio_stream_cancel(hk_audio_owner_t owner)
{
    (void)audio_cancel_owner(owner);
}

uint8_t audio_service_volume(void)
{
    return settings_audio_volume();
}

hk_result_t audio_service_set_volume(uint8_t percent)
{
    if(percent > 100U)
        return HK_ERR_INVALID_ARGUMENT;
    if(percent == settings_audio_volume())
        return HK_OK;
    hk_result_t result = hal_audio_set_volume(percent);
    if(result == HK_OK)
    {
        settings_set_audio_volume(percent);
        settings_mark_dirty(0U);
    }
    return result;
}
#endif
