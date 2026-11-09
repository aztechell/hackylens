#include "hk_config.h"

#if HK_ENABLE_AUDIO_FEATURE
#include "audio_player_view.h"

#include <stdio.h>

#include <hackylens/app/panel.h>

#define PLAYER_BODY_Y 72U
#define PLAYER_BODY_H 153U

static void time_label(char label[16], uint32_t seconds)
{
    (void)snprintf(label, 16U, "%02lu:%02lu", (unsigned long)(seconds / 60U),
                   (unsigned long)(seconds % 60U));
}

hk_result_t audio_player_redraw(const hk_app_context_t *ctx, uint16_t width, uint8_t full)
{
    const hk_display_rect_t body = {2, PLAYER_BODY_Y, width > 4U ? width - 4U : 1U,
                                    PLAYER_BODY_H};
    return hk_app_context_request_render(ctx, full ? NULL : &body);
}

hk_result_t audio_player_draw(hk_app_surface_t *surface, const audio_player_view_t *player,
                              uint8_t volume, const char *message, uint8_t full)
{
    hk_app_panel_t panel = hk_app_panel_begin(surface);
    if(panel.result != HK_OK)
        return panel.result;
    uint16_t width = panel.width - 24U;
    if(full)
    {
        hk_app_panel_chrome(&panel, "WAV PLAYER");
        hk_app_panel_label(&panel, 12U, 39U, width, player->name, HK_PANEL_GREEN, HK_PANEL_CENTER);
    }
    else
        hk_app_panel_fill(&panel, 2U, PLAYER_BODY_Y, panel.width - 4U, PLAYER_BODY_H, HK_PANEL_BLACK);

    char time[16];
    time_label(time, player->position);
    hk_app_panel_label(&panel, 12U, 76U, width / 2U, time, HK_PANEL_GREEN, HK_PANEL_LEFT);
    time_label(time, player->duration);
    hk_app_panel_label(&panel, 12U + width / 2U, 76U, width / 2U, time,
                       HK_PANEL_GREEN, HK_PANEL_RIGHT);

    hk_app_panel_fill(&panel, 12U, 105U, width, 8U, HK_PANEL_TRACK);
    if(player->duration)
    {
        uint32_t elapsed = player->position < player->duration ? player->position : player->duration;
        hk_app_panel_fill(&panel, 12U, 105U, (uint16_t)((uint64_t)width * elapsed / player->duration),
                          8U, HK_PANEL_GREEN);
    }

    hk_app_panel_status(&panel, 126U, message && *message ? message : player->status);
    hk_app_panel_volume(&panel, 181U, volume);
    return panel.result;
}
#endif
