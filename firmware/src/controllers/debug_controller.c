#include "debug_controller.h"

#include "../services/camera_session.h"

#include "../config/debug_config.h"

#include "../core/hk_menu.h"
#include "../core/hk_screen.h"
#include "../core/hk_string.h"
#include "hk_config.h"
#if HK_ENABLE_CAMERA_FEATURE
#include "debug_camera_controller.h"
#endif
#if HK_ENABLE_APP_FACE_DETECT
#include "../services/face_detector.h"
#endif
#include "../services/debug_console_service.h"
#include "../services/screenshot_source.h"
#include "../services/debug_screenshot_stream.h"

static char g_debug_cmd[DEBUG_CMD_MAX];
static uint8_t g_debug_cmd_len;

void debug_uart_handle_command(const char *cmd)
{
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
#if HK_ENABLE_APP_FACE_DETECT
    if(str_eq_ci(cmd, "HKFACEINFO"))
    {
        char line[192];
        face_detector_format_info(line, sizeof(line));
        debug_console_write_text(line);
        return;
    }
#endif
    if(str_eq_ci(cmd, "HKMENU"))
    {
        activity_note();
#if HK_ENABLE_CAMERA_FEATURE
        camera_stop();
#endif
        shell_show_menu();
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
        debug_console_write_text("HKHELP HKSHOT HKFRAME HKCAMINFO HKQRINFO HKFACEINFO HKQR/HKQRCAM HKQRDECODE HKFPS/HKFPSON/HKFPSOFF HKCAMPROBE HKCAMREGS HKCAMDVP HKCAMBAR HKCAMERA HKMENU HKPING\n");
#else
        debug_console_write_text("HKHELP HKSHOT HKMENU HKPING\n");
#endif
        return;
    }
}

void debug_uart_tick(void)
{
    uint8_t raw;

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
            }
            continue;
        }

        if(g_debug_cmd_len + 1U < sizeof(g_debug_cmd))
            g_debug_cmd[g_debug_cmd_len++] = c;
        else
            g_debug_cmd_len = 0;
    }
}
