#include "hk_app_registry.h"

#include <stddef.h>

#include "../../generated/app_registry/registry.h"

const hk_app_t *hk_app_for_screen(screen_t screen)
{
    for(uint8_t i = 0U; i < g_hk_generated_app_count; i++)
    {
        const hk_app_t *app = g_hk_generated_apps[i];
        const hk_legacy_app_entry_t *entry = hk_app_legacy_entry(app);

        if(entry && entry->screen == screen)
            return app;
    }
    for(uint8_t i = 0U; i < g_hk_generated_app_count; i++)
    {
        const hk_app_t *app = g_hk_generated_apps[i];
        const hk_legacy_app_entry_t *entry = hk_app_legacy_entry(app);

        if(entry && entry->owns_screen && entry->owns_screen(screen))
            return app;
    }
    return NULL;
}

const hk_app_t *hk_app_for_autostart_id(hk_autostart_id_t id)
{
    if(id == HK_AUTOSTART_OFF)
        return NULL;
    for(uint8_t i = 0U; i < g_hk_generated_app_count; i++)
    {
        const hk_app_t *app = g_hk_generated_apps[i];

        if(app->autostart_eligible && app->autostart_id == id)
            return app;
    }
    return NULL;
}

uint8_t hk_app_autostart_id_is_persistable(hk_autostart_id_t id)
{
    if(id == HK_AUTOSTART_OFF)
        return 1U;
    for(uint8_t i = 0U; i < g_hk_reserved_autostart_id_count; i++)
    {
        if(g_hk_reserved_autostart_ids[i] == id)
            return 1U;
    }
    return 0U;
}

uint8_t hk_app_autostart_count(void)
{
    uint8_t count = 0U;

    for(uint8_t i = 0U; i < g_hk_generated_app_count; i++)
        count += g_hk_generated_apps[i]->autostart_eligible ? 1U : 0U;
    return count;
}

const hk_app_t *hk_app_autostart_at(uint8_t index)
{
    for(uint8_t i = 0U; i < g_hk_generated_app_count; i++)
    {
        const hk_app_t *app = g_hk_generated_apps[i];

        if(!app->autostart_eligible)
            continue;
        if(index == 0U)
            return app;
        index--;
    }
    return NULL;
}

void hk_app_registry_background_tick(const hk_input_snapshot_t *input)
{
    for(uint8_t i = 0U; i < g_hk_generated_app_count; i++)
    {
        const hk_legacy_app_entry_t *entry =
            hk_app_legacy_entry(g_hk_generated_apps[i]);

        if(entry && entry->background_tick)
            entry->background_tick(input);
    }
}

void hk_app_registry_handle_sd_event(hk_sd_event_t event)
{
    for(uint8_t i = 0U; i < g_hk_generated_app_count; i++)
    {
        const hk_legacy_app_entry_t *entry =
            hk_app_legacy_entry(g_hk_generated_apps[i]);

        if(entry && entry->handle_sd_event)
            entry->handle_sd_event(event);
    }
}

uint8_t hk_app_registry_sd_poll_allowed(screen_t screen)
{
    const hk_app_t *app = hk_app_for_screen(screen);
    const hk_legacy_app_entry_t *entry = hk_app_legacy_entry(app);

    return !entry || !entry->blocks_sd_poll;
}

uint8_t hk_app_registry_handle_debug_command(const char *cmd)
{
    for(uint8_t i = 0U; i < g_hk_generated_app_count; i++)
    {
        const hk_legacy_app_entry_t *entry =
            hk_app_legacy_entry(g_hk_generated_apps[i]);

        if(entry && entry->handle_debug_command &&
           entry->handle_debug_command(cmd))
            return 1U;
    }
    return 0U;
}
