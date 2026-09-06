#include "hk_app_registry.h"

#include <stddef.h>
#include <string.h>
#include <ctype.h>

#include "../../generated/app_registry/registry.h"

const hk_app_t *hk_app_for_id(const char *id)
{
    if(!id) return NULL;
    for(uint8_t i = 0U; i < g_hk_generated_app_count; i++)
        if(strcmp(g_hk_generated_apps[i]->id, id) == 0)
            return g_hk_generated_apps[i];
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

/* Match metadata before dispatch: only the explicitly addressed diagnostic
 * service runs. Never broadcast a command into every inactive application. */
static uint8_t command_matches(const char *commands, const char *command)
{
    if(!commands || !command || !command[0]) return 0U;
    while(*commands)
    {
        const char *start;
        size_t length;
        while(*commands == ' ' || *commands == '/') commands++;
        start = commands;
        while(*commands && *commands != ' ' && *commands != '/') commands++;
        length = (size_t)(commands - start);
        if(strlen(command) == length)
        {
            size_t i;
            for(i = 0U; i < length; i++)
                if(toupper((unsigned char)start[i]) != toupper((unsigned char)command[i])) break;
            if(i == length) return 1U;
        }
    }
    return 0U;
}
uint8_t hk_app_registry_handle_debug_command(const char *cmd)
{
    for(uint8_t i = 0U; i < g_hk_generated_app_count; i++)
    {
        const hk_app_t *app = g_hk_generated_apps[i];
        if(app->debug_command && command_matches(app->debug_help, cmd))
            return app->debug_command(cmd);
    }
    return 0U;
}
