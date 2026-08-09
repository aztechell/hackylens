#include "micropython_app.h"

#include <stdio.h>
#include <string.h>

#include "../../config/input_config.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "../../services/debug_console_service.h"
#include "../../services/micropython_program.h"
#include "../../hal/hal_watchdog.h"
#include "../../services/micropython_runtime.h"
#include "../../storage/userfs.h"
#include "micropython_config.h"
#include "micropython_view.h"

const char g_micropython_debug_help[] =
    "HKMPRUN HKMPTEST HKMPSTOP HKMPSTATUS HKMPLOG HKMPLIST HKMPFORMAT-CONFIRM";

static const char g_smoke_script[] =
    "print('MP_OK', sum(range(10)))\n"
    "for i in range(5):\n"
    "    print('tick', i)\n";
static const char g_stop_test_script[] =
    "print('MP_STOP_TEST_READY')\n"
    "while True:\n"
    "    pass\n";

static char g_startup[USERFS_NAME_MAX + 1U];
static char g_logs[MICROPYTHON_LOG_LINES][MICROPYTHON_LOG_COLUMNS + 1U];
static uint32_t g_observed_run_id;
static uint8_t g_screen_active;
static uint8_t g_exit_pending;
static uint8_t g_dirty;
static uint8_t g_render_divider;
static micropython_runtime_state_t g_last_state;
static micropython_runtime_exit_t g_last_exit;
static uint32_t g_last_dropped;

static uint8_t runtime_active(micropython_runtime_state_t state)
{
    return state == MICROPYTHON_RUNTIME_STARTING ||
           state == MICROPYTHON_RUNTIME_RUNNING ||
           state == MICROPYTHON_RUNTIME_STOPPING;
}

static void log_advance(void)
{
    for(uint8_t line = 0U; line + 1U < MICROPYTHON_LOG_LINES; line++)
        memcpy(g_logs[line], g_logs[line + 1U], sizeof(g_logs[line]));
    memset(g_logs[MICROPYTHON_LOG_LINES - 1U], 0,
           sizeof(g_logs[MICROPYTHON_LOG_LINES - 1U]));
}

static void log_message(const char *message)
{
    if(g_logs[MICROPYTHON_LOG_LINES - 1U][0])
        log_advance();
    snprintf(g_logs[MICROPYTHON_LOG_LINES - 1U],
             sizeof(g_logs[MICROPYTHON_LOG_LINES - 1U]), "%s", message);
    g_dirty = 1U;
}

static void log_bytes(const char *data, size_t size)
{
    char *line = g_logs[MICROPYTHON_LOG_LINES - 1U];
    size_t used = strlen(line);

    for(size_t i = 0U; i < size; i++)
    {
        unsigned char c = (unsigned char)data[i];
        if(c == '\r')
            continue;
        if(c == '\n')
        {
            log_advance();
            line = g_logs[MICROPYTHON_LOG_LINES - 1U];
            used = 0U;
            continue;
        }
        if(used >= MICROPYTHON_LOG_COLUMNS)
        {
            log_advance();
            line = g_logs[MICROPYTHON_LOG_LINES - 1U];
            used = 0U;
        }
        line[used++] = c >= 0x20U && c < 0x7FU ? (char)c : '?';
        line[used] = '\0';
    }
    g_dirty = 1U;
}

static void refresh_startup(void)
{
    g_startup[0] = '\0';
    if(userfs_get_startup(g_startup, sizeof(g_startup)) != USERFS_OK)
        g_startup[0] = '\0';
}

static uint8_t run_selected_script(void)
{
    micropython_program_result_t result;

    refresh_startup();
    if(g_startup[0])
    {
        result = micropython_program_run_file(
            g_startup, MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS, NULL);
        if(result != MICROPYTHON_PROGRAM_OK)
        {
            log_message("STARTUP RUN FAILED");
            return 0U;
        }
    }
    else if(!micropython_runtime_start(
                g_smoke_script, sizeof(g_smoke_script) - 1U,
                MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS))
    {
        log_message("VM START FAILED/BUSY");
        return 0U;
    }
    log_message(g_startup[0] ? "RUN STARTUP" : "RUN SMOKE");
    return 1U;
}

static void render_if_needed(uint8_t force)
{
    micropython_runtime_status_t runtime;
    userfs_status_t filesystem;

    if(!g_screen_active || (!force && !g_dirty))
        return;
    micropython_runtime_get_status(&runtime);
    userfs_get_status(&filesystem);
    micropython_view_render(&runtime, &filesystem, g_startup, g_logs);
    g_dirty = 0U;
}

void micropython_enter(const hk_input_snapshot_t *input)
{
    userfs_result_t mount_result;
    uint8_t watchdog_recovery = hal_watchdog_reset_detected();

    (void)input;
    hk_screen_set(HK_MICROPYTHON_SCREEN);
    g_screen_active = 1U;
    g_exit_pending = 0U;
    memset(g_logs, 0, sizeof(g_logs));
    log_message("MICROPYTHON READY");
    mount_result = userfs_mount();
    if(mount_result != USERFS_OK)
    {
        char message[40];
        snprintf(message, sizeof(message), "USERFS %s",
                 userfs_result_name(mount_result));
        log_message(message);
    }
    refresh_startup();
    if(watchdog_recovery)
        log_message("WDT RECOVERY: STARTUP HELD");
    if(g_startup[0] && !watchdog_recovery)
        (void)run_selected_script();
    g_dirty = 1U;
    render_if_needed(1U);
}

void micropython_exit(void)
{
    micropython_runtime_status_t status;

    g_screen_active = 0U;
    micropython_runtime_get_status(&status);
    if(runtime_active(status.state))
        (void)micropython_runtime_request_stop();
}

void micropython_tick(const hk_input_snapshot_t *input)
{
    (void)input;
    if(++g_render_divider >= 5U)
    {
        g_render_divider = 0U;
        render_if_needed(0U);
    }
}

void micropython_handle_buttons(const hk_input_snapshot_t *input)
{
    micropython_runtime_status_t status;

    micropython_runtime_get_status(&status);
    if(input->pressed & BUTTON_OK)
    {
        if(runtime_active(status.state))
            (void)micropython_runtime_request_stop();
        else
            (void)run_selected_script();
        g_dirty = 1U;
    }
    if(input->pressed & BUTTON_BACK)
    {
        if(runtime_active(status.state))
        {
            g_exit_pending = 1U;
            (void)micropython_runtime_request_stop();
            log_message("STOPPING FOR MENU");
        }
        else
        {
            shell_show_menu();
        }
    }
}

void micropython_background_tick(const hk_input_snapshot_t *input)
{
    char output[160];
    size_t count;
    micropython_runtime_status_t status;

    (void)input;
    micropython_runtime_poll();
    micropython_runtime_get_status(&status);
    if(g_exit_pending && !runtime_active(status.state) && g_screen_active)
    {
        g_exit_pending = 0U;
        shell_show_menu();
        return;
    }
    if(status.state != g_last_state || status.exit_reason != g_last_exit)
    {
        g_last_state = status.state;
        g_last_exit = status.exit_reason;
        g_dirty = 1U;
    }
    if(status.run_id != g_observed_run_id)
    {
        g_observed_run_id = status.run_id;
        g_last_dropped = 0U;
    }
    if(status.output_dropped != g_last_dropped)
    {
        if(status.output_dropped > g_last_dropped)
        {
            char message[40];
            snprintf(message, sizeof(message), "[%lu LOG BYTES LOST]",
                     (unsigned long)(status.output_dropped - g_last_dropped));
            log_message(message);
        }
        g_last_dropped = status.output_dropped;
        g_dirty = 1U;
    }
    for(uint8_t pass = 0U; pass < 4U; pass++)
    {
        count = micropython_runtime_read_output(output, sizeof(output));
        if(!count)
            break;
        log_bytes(output, count);
    }
}

void micropython_draw_icon(uint16_t x, uint16_t y,
                           uint16_t color, uint16_t background)
{
    micropython_view_draw_icon(x, y, color, background);
}

static void debug_status(void)
{
    micropython_runtime_status_t runtime;
    userfs_status_t filesystem;
    char line[192];

    micropython_runtime_get_status(&runtime);
    userfs_get_status(&filesystem);
    snprintf(line, sizeof(line),
             "HKMPSTATUS state=%u exit=%u run=%u source=%u pending=%u dropped=%u fs=%u fs_error=%u startup=%s\n",
             (unsigned)runtime.state, (unsigned)runtime.exit_reason,
             (unsigned)runtime.run_id, (unsigned)runtime.source_bytes,
             (unsigned)runtime.output_pending, (unsigned)runtime.output_dropped,
             (unsigned)filesystem.state, (unsigned)filesystem.last_error,
             g_startup[0] ? g_startup : "OFF");
    debug_console_write_text(line);
}

static uint8_t debug_list_item(const char *name, uint32_t size, void *context)
{
    char line[96];
    (void)context;
    snprintf(line, sizeof(line), "HKMPFILE name=%s size=%lu\n", name,
             (unsigned long)size);
    debug_console_write_text(line);
    return 1U;
}

uint8_t micropython_handle_debug_command(const char *command)
{
    if(str_eq_ci(command, "HKMPRUN"))
    {
        debug_console_write_text(run_selected_script() ? "HKMPRUN OK\n" :
                                                         "HKMPRUN ERROR\n");
        return 1U;
    }
    if(str_eq_ci(command, "HKMPTEST"))
    {
        uint8_t started = micropython_runtime_start(
            g_stop_test_script, sizeof(g_stop_test_script) - 1U,
            MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS);
        debug_console_write_text(started ? "HKMPTEST OK\n" : "HKMPTEST ERROR\n");
        return 1U;
    }
    if(str_eq_ci(command, "HKMPSTOP"))
    {
        debug_console_write_text(micropython_runtime_request_stop() ?
                                 "HKMPSTOP OK\n" : "HKMPSTOP IDLE\n");
        return 1U;
    }
    if(str_eq_ci(command, "HKMPSTATUS"))
    {
        debug_status();
        return 1U;
    }
    if(str_eq_ci(command, "HKMPLOG"))
    {
        for(uint8_t i = 0U; i < MICROPYTHON_LOG_LINES; i++)
        {
            if(g_logs[i][0])
            {
                debug_console_write_text("HKMPLOG ");
                debug_console_write_text(g_logs[i]);
                debug_console_write_text("\n");
            }
        }
        return 1U;
    }
    if(str_eq_ci(command, "HKMPLIST"))
    {
        userfs_result_t result = userfs_list(debug_list_item, NULL);
        if(result != USERFS_OK)
        {
            debug_console_write_text("HKMPLIST ERROR ");
            debug_console_write_text(userfs_result_name(result));
            debug_console_write_text("\n");
        }
        else
            debug_console_write_text("HKMPLIST END\n");
        return 1U;
    }
    if(str_eq_ci(command, "HKMPFORMAT-CONFIRM"))
    {
        userfs_result_t result = userfs_format_explicit();
        debug_console_write_text(result == USERFS_OK ? "HKMPFORMAT OK\n" :
                                                       "HKMPFORMAT ERROR\n");
        refresh_startup();
        g_dirty = 1U;
        return 1U;
    }
    return 0U;
}
