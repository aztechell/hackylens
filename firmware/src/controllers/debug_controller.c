#include "debug_controller.h"

#include "../config/debug_config.h"

#include "../core/hk_menu_runtime.h"
#include "../core/hk_app_registry.h"
#include "../core/hk_screen.h"
#include "../core/hk_string.h"
#include "hk_config.h"
#if HK_ENABLE_CAMERA_FEATURE
#include "debug_camera_controller.h"
#endif
#include "../services/debug_console_service.h"
#include "../services/screenshot_source.h"
#include "../services/debug_screenshot_stream.h"
#include "../services/external_link_service.h"
#if HK_ENABLE_APP_MICROPYTHON
#include "../services/hmpy_codec.h"
#include "../services/hmpy_session.h"
#endif

static char g_debug_cmd[DEBUG_CMD_MAX];
static uint8_t g_debug_cmd_len;

void debug_uart_handle_command(const char *cmd)
{
#if HK_ENABLE_APP_MICROPYTHON
    if(str_eq_ci(cmd, HMPY_LINE_HANDSHAKE))
    {
        hmpy_session_begin();
        return;
    }
#endif
    if(external_link_service_handle_debug_command(cmd))
        return;
    if(str_eq_ci(cmd, "HKSHOT") || str_eq_ci(cmd, "SHOT") || str_eq_ci(cmd, "SCREENSHOT"))
    {
        activity_note();
        debug_uart_send_screenshot(screenshot_source_lcd_shadow());
        return;
    }
#if HK_ENABLE_CAMERA_FEATURE
    if(debug_camera_controller_handle_command(cmd))
        return;
#endif
    if(hk_app_registry_handle_debug_command(cmd))
        return;
    if(str_eq_ci(cmd, "HKMENU"))
    {
        activity_note();
        shell_show_menu_reason(HK_APP_STOP_FORCED);
        return;
    }
    if(str_eq_ci(cmd, "HKSETTINGS"))
    {
        const hk_app_t *settings = NULL;
        uint8_t index;

        activity_note();
        for(index = 0U; index < g_menu_item_count; index++)
        {
            const hk_app_t *app = g_menu_items[index];

            if(app && app->id && str_eq_ci(app->id, "settings"))
            {
                settings = app;
                break;
            }
        }
        if(!settings)
            return;
        if(hk_screen_get() != SCREEN_MENU)
            shell_show_menu();
        (void)shell_open_app(settings, NULL);
        return;
    }
    if(str_eq_ci(cmd, "HKPING"))
    {
        debug_console_write_text("HKPONG\n");
        return;
    }
    if(str_eq_ci(cmd, "HKHELP"))
    {
#if HK_ENABLE_CAMERA_FEATURE
        debug_console_write_text("HKHELP HKSHOT HKFRAME HKCAMINFO HKFPS/HKFPSON/HKFPSOFF HKCAMPROBE HKCAMREGS HKCAMDVP HKCAMBAR ");
#else
        debug_console_write_text("HKHELP HKSHOT ");
#endif
        for(uint8_t i = 0; i < g_menu_item_count; i++)
        {
            const hk_app_t *app = g_menu_items[i];
            const hk_legacy_app_entry_t *entry = hk_app_legacy_entry(app);

            if(entry && entry->handle_debug_command && app->debug_help)
            {
                debug_console_write_text(app->debug_help);
                debug_console_write_text(" ");
            }
        }
        debug_console_write_text("HKLINKINFO HKLINKUART HKLINKI2C HKLINK9600 HKLINK115200 HKLINK1000000 HKMENU HKSETTINGS HKPING\n");
        return;
    }
}

void debug_uart_tick(void)
{
    uint8_t raw;

#if HK_ENABLE_APP_MICROPYTHON
    if(hmpy_session_active())
    {
        hmpy_session_tick();
        return;
    }
#endif

    while(debug_console_read(&raw, 1) == 1)
    {
        char c = (char)raw;

        if(c == '\r' || c == '\n')
        {
            if(g_debug_cmd_len)
            {
                g_debug_cmd[g_debug_cmd_len] = '\0';
                debug_uart_handle_command(g_debug_cmd);
                g_debug_cmd_len = 0;
#if HK_ENABLE_APP_MICROPYTHON
                if(hmpy_session_active())
                {
                    hmpy_session_tick();
                    return;
                }
#endif
            }
            continue;
        }

        if(g_debug_cmd_len + 1U < sizeof(g_debug_cmd))
            g_debug_cmd[g_debug_cmd_len++] = c;
        else
            g_debug_cmd_len = 0;
    }
}
