#include "hk_config.h"

#if HK_ENABLE_APP_RECORDER
#include <hackylens/app/recorder.h>

#include <string.h>

#include "../services/audio_player.h"
#include "../services/audio_recordings.h"
#include "../ui/audio_player_view.h"

#define RECORDER_OWNER HK_AUDIO_OWNER_RECORDER

static hk_result_t validate_recorder(const hk_app_context_t *ctx)
{
    const char *id;
    uint32_t generation;
    hk_result_t result = hk_app_context_identity(ctx, &id, &generation);
    if(result != HK_OK)
        return result;
    return id && generation && !strcmp(id, "recorder") ? HK_OK : HK_ERR_NOT_DECLARED;
}

hk_result_t hk_recorder_player_redraw(const hk_app_context_t *ctx, uint16_t width, uint8_t full)
{
    hk_result_t result = validate_recorder(ctx);
    return result == HK_OK ? audio_player_redraw(ctx, width, full) : result;
}

hk_result_t hk_recorder_player_render(const hk_app_context_t *ctx, hk_app_surface_t *surface,
                                      const char *message, uint8_t full)
{
    hk_result_t result = validate_recorder(ctx);
    if(result != HK_OK)
        return result;
    audio_player_view_t player = audio_player_view(RECORDER_OWNER);
    return audio_player_draw(surface, &player, audio_service_volume(), message, full);
}

hk_result_t hk_recorder_info(const hk_app_context_t *ctx, hk_recorder_info_t *info)
{
    hk_result_t result = validate_recorder(ctx);
    if(result != HK_OK)
        return result;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    audio_recording_view_t recording = audio_recording_view();
    audio_player_view_t player = audio_player_view(RECORDER_OWNER);
    *info = (hk_recorder_info_t){
        .record_status = recording.status,
        .player_name = player.name,
        .player_status = player.status,
        .recorded_seconds = recording.seconds,
        .player_position = player.position,
        .player_duration = player.duration,
        .recording = recording.active,
        .saving = recording.stopping,
        .preparing = recording.preparing,
        .playing = player.playing,
        .count = audio_recordings_count(),
        .volume = audio_service_volume(),
    };
    return HK_OK;
}

const char *hk_recorder_name(const hk_app_context_t *ctx, uint8_t index)
{
    if(validate_recorder(ctx) != HK_OK)
        return NULL;
    const fat_file_entry_t *entry = audio_recordings_get(index);
    return entry ? entry->name : NULL;
}

const char *hk_recorder_action(const hk_app_context_t *ctx, hk_recorder_action_t action,
                               int16_t value)
{
    if(validate_recorder(ctx) != HK_OK)
        return "AUDIO UNAVAILABLE";
    switch(action)
    {
    case HK_RECORDER_START:
        audio_recording_begin();
        return audio_recording_view().status;
    case HK_RECORDER_SAVE:
        audio_recording_end();
        break;
    case HK_RECORDER_LIST:
        return audio_recordings_load();
    case HK_RECORDER_OPEN:
    {
        const fat_file_entry_t *entry = value >= 0 && value <= UINT8_MAX ?
            audio_recordings_get((uint8_t)value) : NULL;
        return entry ? audio_player_open(RECORDER_OWNER, entry) : "NO RECORDING";
    }
    case HK_RECORDER_PLAY_STOP:
        audio_player_toggle(RECORDER_OWNER);
        break;
    case HK_RECORDER_CLOSE_PLAYER:
        audio_player_close(RECORDER_OWNER);
        break;
    case HK_RECORDER_ADJUST_VOLUME:
        return (value == -1 || value == 1) ? audio_volume_adjust((int8_t)value) :
            "VOLUME ERROR";
    case HK_RECORDER_MEDIA_REMOVED:
        audio_player_close(RECORDER_OWNER);
        audio_recording_close(1U);
        break;
    }
    return NULL;
}

hk_result_t hk_recorder_retire(const hk_app_context_t *ctx, hk_deadline_t deadline)
{
    hk_result_t result = validate_recorder(ctx);
    if(result != HK_OK)
        return result;
    audio_player_close(RECORDER_OWNER);
    /* A forced app switch must not start SD writes during teardown. */
    audio_recording_close(1U);
    const hk_time_t *time;
    uint64_t now;
    result = hk_app_context_time(ctx, &time);
    if(result == HK_OK)
        result = hk_time_now_us(time, &now);
    return result == HK_OK && now > deadline.at_us ? HK_ERR_DEADLINE_EXCEEDED : result;
}
#endif
