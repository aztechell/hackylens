#include <stdio.h>

#include <hackylens/app/runtime.h>

#include "firmware/generated/app_registry/registry.h"
#include "firmware/src/core/hk_app_registry.h"

/* Registry lookups exercise the generated descriptors without launching apps. */
const hk_app_v2_entry_t alpha_tool_app = {0};
const hk_app_v2_entry_t zeta_legacy_enter = {0};

void alpha_tool_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t background)
{
    (void)x;
    (void)y;
    (void)color;
    (void)background;
}

void zeta_legacy_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t background)
{
    alpha_tool_draw_icon(x, y, color, background);
}

int main(void)
{
    fputs("{\"enabled\":[", stdout);
    for(uint8_t i = 0; i < g_hk_generated_app_count; ++i)
        printf("%s\"%s\"", i ? "," : "", g_hk_generated_apps[i]->id);

    fputs("],\"reserved_ids\":[", stdout);
    for(uint8_t i = 0; i < g_hk_reserved_autostart_id_count; ++i)
        printf("%s%u", i ? "," : "", (unsigned)g_hk_reserved_autostart_ids[i]);

    /* Observe the lookup of the persisted fixture ID through the runtime API. */
    const hk_autostart_id_t persisted = 42;
    const hk_app_t *app = hk_app_for_autostart_id(persisted);
    fputs("],\"persisted_lookup\":", stdout);
    if(app)
        printf("\"%s\"", app->id);
    else
        fputs("null", stdout);
    printf(",\"persistable\":%s,\"peer_lookup\":",
           hk_app_autostart_id_is_persistable(persisted) ? "true" : "false");
    const hk_app_t *peer = hk_app_for_autostart_id(43);
    if(peer)
        printf("\"%s\"", peer->id);
    else
        fputs("null", stdout);
    puts("}");
    return 0;
}
