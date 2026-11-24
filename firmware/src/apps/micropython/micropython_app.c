#include "micropython_app.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../../adapters/micropython/micropython_capability_bridge.h"
#include "../../config/input_config.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "../../internal/boot_internal.h"
#include "../../services/debug_console_service.h"
#include "../../services/micropython_program.h"
#include "../../services/micropython_runtime.h"
#include "../../storage/userfs.h"
#include "micropython_config.h"
#include "micropython_preview.h"
#include "micropython_view.h"

static const char g_smoke_script[] =
    "print('MP_OK', sum(range(10)))\n"
    "for i in range(5):\n"
    "    print('tick', i)\n";
static const char g_stop_test_script[] =
    "print('MP_STOP_TEST_READY')\n"
    "while True:\n"
    "    pass\n";

typedef struct
{
    uint32_t count;
    uint32_t found_index;
    const char *wanted_name;
    uint8_t found;
} file_count_context_t;

typedef struct
{
    uint32_t top;
    uint8_t count;
    micropython_file_row_t files[3U * MICROPYTHON_LIST_VISIBLE_ROWS];
} file_window_t;

typedef struct
{
    file_window_t *window;
    uint32_t index;
} file_window_scan_t;

static micropython_view_state_t g_ui;
static file_window_t g_files;
static userfs_status_t g_filesystem;
static char g_logs[MICROPYTHON_LOG_HISTORY_LINES]
                  [MICROPYTHON_LOG_COLUMNS + 1U];
static uint8_t g_log_start;
static uint8_t g_log_count;
static uint32_t g_preview_revision;
static uint8_t g_preview_retry;
static uint32_t g_observed_run_id;
static uint8_t g_screen_active;
static uint8_t g_close_requested;
static uint8_t g_opened_from_pin;
static uint8_t g_close_after_stop;
static micropython_view_update_t g_render_update;
static uint8_t g_render_divider;
static uint8_t g_console_render_ticks;
static uint8_t g_console_live_dirty;
static uint32_t g_list_revision;
static uint64_t g_list_retry_us;
static uint8_t g_list_valid;
static uint32_t g_repeat_button;
static uint64_t g_repeat_due_us;
static micropython_runtime_state_t g_last_state;
static micropython_runtime_exit_t g_last_exit;
static uint32_t g_last_dropped;

static void request_full_render(void)
{
    memset(&g_render_update, 0, sizeof(g_render_update));
    g_render_update.full = 1U;
}

static void request_info_render(void)
{
    if(!g_render_update.full)
        g_render_update.info = 1U;
}

static void request_body_render(void)
{
    if(!g_render_update.full)
    {
        g_render_update.body = 1U;
        g_render_update.row_mask = 0U;
    }
}

static void request_row_render(uint8_t row)
{
    if(!g_render_update.full && !g_render_update.body && row < 8U)
        g_render_update.row_mask |= (uint8_t)(1U << row);
}

static uint8_t render_pending(void)
{
    return g_render_update.full || g_render_update.info ||
           g_render_update.body || g_render_update.row_mask;
}

static uint8_t runtime_active(micropython_runtime_state_t state)
{
    return state == MICROPYTHON_RUNTIME_STARTING ||
           state == MICROPYTHON_RUNTIME_RUNNING ||
           state == MICROPYTHON_RUNTIME_STOPPING;
}

static void set_status(const char *format, ...)
{
    va_list arguments;
    char next[MICROPYTHON_STATUS_COLUMNS + 1U];

    if(!format || !format[0])
        next[0] = '\0';
    else
    {
        va_start(arguments, format);
        vsnprintf(next, sizeof(next), format, arguments);
        va_end(arguments);
    }
    if(strcmp(g_ui.status, next) == 0)
        return;
    snprintf(g_ui.status, sizeof(g_ui.status), "%s", next);
    request_info_render();
}

static uint8_t log_last_index(void)
{
    return (uint8_t)((g_log_start + g_log_count - 1U) %
                     MICROPYTHON_LOG_HISTORY_LINES);
}

static void log_reset(void)
{
    memset(g_logs, 0, sizeof(g_logs));
    g_log_start = 0U;
    g_log_count = 1U;
    g_ui.log_follow = 1U;
    g_ui.log_top = 0U;
    g_console_live_dirty = 1U;
}

static void log_new_line(void)
{
    uint8_t index;

    if(g_log_count < MICROPYTHON_LOG_HISTORY_LINES)
    {
        index = (uint8_t)((g_log_start + g_log_count) %
                          MICROPYTHON_LOG_HISTORY_LINES);
        g_log_count++;
    }
    else
    {
        g_log_start = (uint8_t)((g_log_start + 1U) %
                                MICROPYTHON_LOG_HISTORY_LINES);
        index = log_last_index();
        if(!g_ui.log_follow && g_ui.log_top)
            g_ui.log_top--;
    }
    memset(g_logs[index], 0, sizeof(g_logs[index]));
}

static void log_bytes(const char *data, size_t size)
{
    for(size_t i = 0U; i < size; i++)
    {
        unsigned char c = (unsigned char)data[i];
        char *line;
        size_t used;

        if(c == '\r')
            continue;
        if(c == '\n')
        {
            log_new_line();
            continue;
        }
        line = g_logs[log_last_index()];
        used = strlen(line);
        if(used >= MICROPYTHON_LOG_COLUMNS)
        {
            log_new_line();
            line = g_logs[log_last_index()];
            used = 0U;
        }
        line[used++] = c >= 0x20U && c < 0x7FU ? (char)c : '?';
        line[used] = '\0';
    }
    g_console_live_dirty = 1U;
}

static void log_message(const char *message)
{
    if(g_logs[log_last_index()][0])
        log_new_line();
    log_bytes(message, strlen(message));
    log_new_line();
}

static uint32_t log_effective_count(void)
{
    if(g_log_count == 1U && !g_logs[g_log_start][0])
        return 0U;
    if(g_log_count > 1U && !g_logs[log_last_index()][0])
        return g_log_count - 1U;
    return g_log_count;
}

static const char *log_at(uint32_t logical_index)
{
    uint8_t index = (uint8_t)((g_log_start + logical_index) %
                              MICROPYTHON_LOG_HISTORY_LINES);
    return g_logs[index];
}

static void prepare_log_view(void)
{
    uint32_t total = log_effective_count();
    uint32_t maximum_top = total > MICROPYTHON_LOG_VISIBLE_LINES ?
                           total - MICROPYTHON_LOG_VISIBLE_LINES : 0U;

    if(g_ui.log_follow)
        g_ui.log_top = maximum_top;
    else if(g_ui.log_top > maximum_top)
        g_ui.log_top = maximum_top;
    g_ui.log_total = total;
    g_ui.log_count = (uint8_t)(total - g_ui.log_top);
    if(g_ui.log_count > MICROPYTHON_LOG_VISIBLE_LINES)
        g_ui.log_count = MICROPYTHON_LOG_VISIBLE_LINES;
    memset(g_ui.log_lines, 0, sizeof(g_ui.log_lines));
    for(uint8_t row = 0U; row < g_ui.log_count; row++)
        snprintf(g_ui.log_lines[row], sizeof(g_ui.log_lines[row]), "%s",
                 log_at(g_ui.log_top + row));
}

static uint8_t python_file_name(const char *name)
{
    size_t length = name ? strlen(name) : 0U;

    return length > 3U && name[length - 3U] == '.' &&
           (name[length - 2U] == 'p' || name[length - 2U] == 'P') &&
           (name[length - 1U] == 'y' || name[length - 1U] == 'Y');
}

static void read_startup(char startup[USERFS_NAME_MAX + 1U])
{
    if(userfs_get_startup(startup, USERFS_NAME_MAX + 1U) != USERFS_OK)
        startup[0] = '\0';
}

static uint8_t count_file(const char *name, uint32_t size, void *context)
{
    file_count_context_t *scan = (file_count_context_t *)context;
    (void)size;

    if(!python_file_name(name))
        return 1U;
    if(scan->wanted_name && scan->wanted_name[0] &&
       strcmp(scan->wanted_name, name) == 0)
    {
        scan->found = 1U;
        scan->found_index = scan->count;
    }
    scan->count++;
    return 1U;
}

static uint8_t collect_file(const char *name, uint32_t size, void *context)
{
    file_window_scan_t *scan = context;
    file_window_t *window = scan->window;

    if(!python_file_name(name) || scan->index++ < window->top)
        return 1U;
    micropython_file_row_t *row = &window->files[window->count++];
    memcpy(row->name, name, strlen(name) + 1U);
    row->size = size;
    return window->count < sizeof(window->files) / sizeof(window->files[0]);
}

static uint32_t list_top_for(uint32_t index, uint32_t count)
{
    uint32_t top = g_ui.list_top;
    uint32_t rows = MICROPYTHON_LIST_VISIBLE_ROWS;
    uint32_t limit = count > rows ? count - rows : 0U;

    if(index < top)
        top = index;
    else if(index >= top + rows)
        top = index - rows + 1U;
    return top < limit ? top : limit;
}

static uint8_t set_file_selection(uint32_t index, uint32_t count,
                                  const char *startup, uint8_t refresh)
{
    uint32_t top = list_top_for(index, count);
    uint32_t visible = count - top;
    if(visible > MICROPYTHON_LIST_VISIBLE_ROWS)
        visible = MICROPYTHON_LIST_VISIBLE_ROWS;

    if(visible && (refresh || top < g_files.top ||
                   top + visible > g_files.top + g_files.count))
    {
        file_window_t window = {0};
        file_window_scan_t scan = { .window = &window };
        window.top = top > MICROPYTHON_LIST_VISIBLE_ROWS ?
                     top - MICROPYTHON_LIST_VISIBLE_ROWS : 0U;
        userfs_result_t result = userfs_list(collect_file, &scan);
        if(result != USERFS_OK || top + visible > window.top + window.count)
        {
            set_status("LIST %s", userfs_result_name(
                result == USERFS_OK ? USERFS_ERROR_IO : result));
            return 0U;
        }
        g_files = window;
    }

    if(refresh || top != g_ui.list_top || visible != g_ui.visible_count)
    {
        for(uint8_t row = 0U; row < visible; ++row)
        {
            micropython_file_row_t *file = &g_ui.files[row];
            *file = g_files.files[top - g_files.top + row];
            file->startup = startup[0] && strcmp(startup, file->name) == 0;
        }
    }
    g_ui.file_count = count;
    g_ui.list_top = top;
    g_ui.visible_count = (uint8_t)visible;
    g_ui.selected_index = index;
    g_ui.selected_name[0] = '\0';
    g_ui.selected_size = 0U;
    if(visible)
    {
        const micropython_file_row_t *selected = &g_ui.files[index - top];
        memcpy(g_ui.selected_name, selected->name, sizeof(g_ui.selected_name));
        g_ui.selected_size = selected->size;
    }
    return 1U;
}

static uint8_t refresh_file_list(uint8_t preserve_name, uint8_t mark_changes)
{
    char startup[USERFS_NAME_MAX + 1U] = {0};
    uint32_t index = g_ui.selected_index;
    read_startup(startup);
    file_count_context_t scan = {
        .wanted_name = preserve_name ?
            (g_ui.selected_name[0] ? g_ui.selected_name : startup) : NULL,
    };
    userfs_result_t result = userfs_list(count_file, &scan);
    if(result != USERFS_OK)
    {
        set_status("LIST %s", userfs_result_name(result));
        return 0U;
    }
    if(scan.found)
        index = scan.found_index;
    else if(index >= scan.count)
        index = scan.count ? scan.count - 1U : 0U;
    if(!set_file_selection(index, scan.count, startup, 1U))
        return 0U;
    memcpy(g_ui.startup, startup, sizeof(g_ui.startup));
    g_list_revision = userfs_revision();
    g_list_valid = 1U;
    userfs_get_status(&g_filesystem);
    if(strncmp(g_ui.status, "LIST ", 5U) == 0)
        set_status("");
    if(mark_changes)
    {
        request_info_render();
        request_body_render();
    }
    return 1U;
}

static void set_preview_status(userfs_result_t result)
{
    if(result != USERFS_OK)
        set_status("VIEW %s", userfs_result_name(result));
    else
        set_status("");
    request_info_render();
    request_body_render();
}

static void open_preview(void)
{
    g_preview_revision = userfs_revision();
    userfs_result_t result = micropython_preview_open(
        &g_ui, micropython_view_code_columns());
    g_preview_retry = result == USERFS_ERROR_BUSY;
    set_preview_status(result);
}

static void render_if_needed(uint8_t force)
{
    micropython_runtime_status_t runtime;

    if(!g_screen_active)
        return;
    if(micropython_capability_bridge_camera_presented())
    {
        if(!g_render_update.full)
            request_full_render();
        return;
    }
    if(force)
        request_full_render();
    if(!render_pending())
        return;
    micropython_runtime_get_status(&runtime);
    if(g_ui.mode == MICROPYTHON_UI_CONSOLE)
        prepare_log_view();
    micropython_view_render(&g_ui, &runtime, &g_filesystem, &g_render_update);
    if(g_ui.mode == MICROPYTHON_UI_CONSOLE &&
       (g_render_update.full || g_render_update.body))
    {
        g_console_live_dirty = 0U;
        g_console_render_ticks = 0U;
    }
    memset(&g_render_update, 0, sizeof(g_render_update));
}

static void show_console(void)
{
    uint8_t mode_changed = g_ui.mode != MICROPYTHON_UI_CONSOLE;

    g_ui.mode = MICROPYTHON_UI_CONSOLE;
    g_ui.log_follow = 1U;
    if(mode_changed)
        request_full_render();
    else
    {
        request_info_render();
        request_body_render();
    }
}

static uint8_t run_debug_default(void)
{
    micropython_program_result_t result;
    char startup[USERFS_NAME_MAX + 1U];

    read_startup(startup);
    log_reset();
    if(startup[0])
    {
        result = micropython_program_run_file(
            startup, MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS, NULL);
        if(result != MICROPYTHON_PROGRAM_OK)
        {
            log_message("STARTUP RUN FAILED");
            return 0U;
        }
        log_message("RUN STARTUP");
    }
    else if(!micropython_runtime_start(
                g_smoke_script, sizeof(g_smoke_script) - 1U,
                MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS))
    {
        log_message("VM START FAILED/BUSY");
        return 0U;
    }
    else
        log_message("RUN SMOKE");
    if(g_screen_active)
        show_console();
    return 1U;
}

static void run_file(const char *name)
{
    micropython_program_result_t result;
    uint32_t run_id = 0U;
    char message[USERFS_NAME_MAX + 8U];

    if(!name[0])
        return;
    log_reset();
    snprintf(message, sizeof(message), "RUN %s", name);
    log_message(message);
    result = micropython_program_run_file(
        name, MICROPYTHON_RUNTIME_UNTIL_STOPPED, &run_id);
    if(result != MICROPYTHON_PROGRAM_OK)
    {
        set_status("RUN %s", micropython_program_result_name(result));
        log_message(g_ui.status);
        if(g_opened_from_pin)
            show_console();
        return;
    }
    g_observed_run_id = run_id;
    set_status("");
    show_console();
}

static void navigate_files(int8_t delta)
{
    if(g_ui.file_count < 2U)
        return;
    uint32_t previous_index = g_ui.selected_index;
    uint32_t previous_top = g_ui.list_top;
    uint32_t index;
    if(delta < 0)
        index = previous_index ? previous_index - 1U : g_ui.file_count - 1U;
    else
        index = (previous_index + 1U) % g_ui.file_count;
    if(!set_file_selection(index, g_ui.file_count, g_ui.startup, 0U))
        return;
    set_status("");
    if(previous_top != g_ui.list_top)
        request_body_render();
    else
    {
        request_row_render((uint8_t)(previous_index - previous_top));
        request_row_render((uint8_t)(index - previous_top));
    }
}

static void navigate_actions(int8_t delta)
{
    uint8_t previous = g_ui.action_index;

    if(delta < 0)
        g_ui.action_index = g_ui.action_index ?
                            g_ui.action_index - 1U :
                            MICROPYTHON_ACTION_COUNT - 1U;
    else
        g_ui.action_index = (uint8_t)((g_ui.action_index + 1U) %
                                      MICROPYTHON_ACTION_COUNT);
    set_status("");
    request_row_render(previous);
    request_row_render(g_ui.action_index);
}

static void navigate_preview(int8_t delta)
{
    if(g_preview_revision != userfs_revision())
        open_preview();
    else if((delta < 0 && g_ui.preview_has_previous) ||
            (delta > 0 && g_ui.preview_has_next))
        set_preview_status(micropython_preview_move(&g_ui, delta));
}

static void execute_action(void)
{
    userfs_result_t result;

    switch((micropython_action_t)g_ui.action_index)
    {
    case MICROPYTHON_ACTION_RUN:
        run_file(g_ui.selected_name);
        break;
    case MICROPYTHON_ACTION_VIEW:
        g_ui.mode = MICROPYTHON_UI_PREVIEW;
        g_list_retry_us = 0U;
        open_preview();
        request_full_render();
        break;
    case MICROPYTHON_ACTION_LOGS:
        show_console();
        break;
    case MICROPYTHON_ACTION_STARTUP:
        if(g_ui.startup[0] &&
           strcmp(g_ui.startup, g_ui.selected_name) == 0)
            result = userfs_set_startup("");
        else
            result = userfs_set_startup(g_ui.selected_name);
        if(result == USERFS_OK)
        {
            (void)refresh_file_list(1U, 0U);
            set_status(g_ui.startup[0] ? "STARTUP SET" : "STARTUP CLEARED");
            request_row_render(MICROPYTHON_ACTION_STARTUP);
        }
        else
            set_status("STARTUP %s", userfs_result_name(result));
        break;
    case MICROPYTHON_ACTION_DELETE:
        g_ui.mode = MICROPYTHON_UI_DELETE_CONFIRM;
        set_status("");
        request_full_render();
        break;
    case MICROPYTHON_ACTION_PIN:
        result = userfs_set_pinned(g_ui.selected_name, !g_ui.selected_pinned);
        if(result == USERFS_OK)
        {
            g_ui.selected_pinned = userfs_is_pinned(g_ui.selected_name);
            set_status(g_ui.selected_pinned ? "PINNED TO MENU" : "PIN REMOVED");
            request_row_render(MICROPYTHON_ACTION_PIN);
        }
        else
            set_status("PIN %s", userfs_result_name(result));
        break;
    default:
        break;
    }
}

static void delete_selected(void)
{
    char deleted[USERFS_NAME_MAX + 1U];
    userfs_result_t result;

    snprintf(deleted, sizeof(deleted), "%s", g_ui.selected_name);
    result = userfs_remove(deleted);
    if(result != USERFS_OK)
    {
        set_status("DELETE %s", userfs_result_name(result));
        return;
    }
    g_ui.mode = MICROPYTHON_UI_LIST;
    (void)refresh_file_list(0U, 0U);
    set_status("DELETED %s", deleted);
    request_full_render();
}

static void request_runtime_stop(void)
{
    micropython_runtime_status_t status;

    micropython_runtime_get_status(&status);
    if(status.state != MICROPYTHON_RUNTIME_STOPPING &&
       micropython_runtime_request_stop())
        log_message("STOP REQUESTED");
    if(g_ui.mode == MICROPYTHON_UI_CONSOLE)
    {
        request_info_render();
        request_body_render();
    }
}

void micropython_enter(const hk_input_snapshot_t *input)
{
    userfs_result_t mount_result;
    micropython_runtime_status_t runtime;
    uint8_t watchdog_recovery = boot_internal_watchdog_reset_detected();

    (void)input;
    g_close_requested = 0U;
    g_close_after_stop = 0U;
    g_opened_from_pin = 0U;
    g_screen_active = 1U;
    memset(&g_render_update, 0, sizeof(g_render_update));
    g_render_divider = 0U;
    g_console_render_ticks = 0U;
    g_console_live_dirty = 0U;
    g_repeat_button = 0U;
    g_list_retry_us = 0U;
    g_list_valid = 0U;
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.mode = MICROPYTHON_UI_LIST;
    g_ui.log_follow = 1U;
    log_reset();
    mount_result = userfs_mount();
    if(mount_result != USERFS_OK)
    {
        g_list_revision = userfs_revision();
        g_list_valid = 1U;
        userfs_get_status(&g_filesystem);
        set_status("USERFS %s", userfs_result_name(mount_result));
    }
    else if(!refresh_file_list(1U, 0U))
        userfs_get_status(&g_filesystem);
    micropython_runtime_get_status(&runtime);
    g_observed_run_id = runtime.run_id;
    g_last_state = runtime.state;
    g_last_exit = runtime.exit_reason;
    g_last_dropped = runtime.output_dropped;
    if(runtime_active(runtime.state))
        show_console();
    if(watchdog_recovery)
        set_status("WDT RECOVERY: PREVIOUS RUN RESET");
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

void micropython_tick(uint64_t now_us)
{
    if(g_close_after_stop && !runtime_active(g_last_state))
    {
        g_close_requested = 1U;
        return;
    }
    if(g_ui.mode == MICROPYTHON_UI_LIST && now_us >= g_list_retry_us &&
       (!g_list_valid || g_list_revision != userfs_revision()))
    {
        if(!refresh_file_list(1U, 1U))
            g_list_retry_us = now_us + MICROPYTHON_LIST_RETRY_US;
    }
    if(g_ui.mode == MICROPYTHON_UI_PREVIEW && now_us >= g_list_retry_us &&
       (g_preview_retry || g_preview_revision != userfs_revision()))
    {
        open_preview();
        if(g_preview_retry)
            g_list_retry_us = now_us + MICROPYTHON_LIST_RETRY_US;
    }
    if(g_repeat_button && now_us >= g_repeat_due_us)
    {
        int8_t delta = g_repeat_button == BUTTON_LEFT ? -1 : 1;
        g_repeat_due_us = now_us + MICROPYTHON_NAV_REPEAT_US;
        if(g_ui.mode == MICROPYTHON_UI_LIST)
            navigate_files(delta);
        else if(g_ui.mode == MICROPYTHON_UI_PREVIEW)
            navigate_preview(delta);
        else
            g_repeat_button = 0U;
    }
    if(g_ui.mode == MICROPYTHON_UI_CONSOLE && g_console_live_dirty)
    {
        if(++g_console_render_ticks >= MICROPYTHON_CONSOLE_RENDER_TICKS)
        {
            g_console_render_ticks = 0U;
            g_console_live_dirty = 0U;
            request_info_render();
            request_body_render();
        }
    }
    else
        g_console_render_ticks = 0U;
    if(g_ui.mode != MICROPYTHON_UI_CONSOLE ||
       ++g_render_divider >= MICROPYTHON_RENDER_TICKS)
    {
        g_render_divider = 0U;
        render_if_needed(0U);
    }
}

static void handle_list_buttons(uint32_t pressed,
                                micropython_runtime_state_t runtime_state)
{
    if(pressed & BUTTON_BACK)
    {
        if(runtime_active(runtime_state))
        {
            show_console();
            request_runtime_stop();
        }
        else
            g_close_requested = 1U;
        return;
    }
    if(pressed & BUTTON_LEFT)
        navigate_files(-1);
    else if(pressed & BUTTON_RIGHT)
        navigate_files(1);
    else if((pressed & BUTTON_OK) && g_ui.file_count)
    {
        if(g_list_revision != userfs_revision())
        {
            char selected[USERFS_NAME_MAX + 1U];
            memcpy(selected, g_ui.selected_name, sizeof(selected));
            if(!refresh_file_list(1U, 1U) ||
               strcmp(selected, g_ui.selected_name) != 0)
                return;
        }
        g_ui.selected_pinned = userfs_is_pinned(g_ui.selected_name);
        g_ui.action_index = MICROPYTHON_ACTION_RUN;
        g_ui.mode = MICROPYTHON_UI_ACTIONS;
        set_status("");
        request_full_render();
    }
}

static void handle_action_buttons(uint32_t pressed)
{
    if(pressed & BUTTON_BACK)
    {
        g_ui.mode = MICROPYTHON_UI_LIST;
        set_status("");
        request_full_render();
    }
    else if(pressed & BUTTON_LEFT)
        navigate_actions(-1);
    else if(pressed & BUTTON_RIGHT)
        navigate_actions(1);
    else if(pressed & BUTTON_OK)
        execute_action();
}

static void handle_preview_buttons(uint32_t pressed)
{
    if(pressed & BUTTON_BACK)
    {
        g_ui.mode = MICROPYTHON_UI_ACTIONS;
        set_status("");
        request_full_render();
    }
    else if(pressed & BUTTON_LEFT)
        navigate_preview(-1);
    else if(pressed & BUTTON_RIGHT)
        navigate_preview(1);
}

static void handle_console_buttons(uint32_t pressed,
                                   micropython_runtime_state_t runtime_state)
{
    uint32_t total = log_effective_count();
    uint32_t maximum_top = total > MICROPYTHON_LOG_VISIBLE_LINES ?
                           total - MICROPYTHON_LOG_VISIBLE_LINES : 0U;

    if(pressed & BUTTON_LEFT)
    {
        g_ui.log_follow = 0U;
        g_ui.log_top = g_ui.log_top > MICROPYTHON_LOG_VISIBLE_LINES ?
                       g_ui.log_top - MICROPYTHON_LOG_VISIBLE_LINES : 0U;
        request_info_render();
        request_body_render();
    }
    else if(pressed & BUTTON_RIGHT)
    {
        if(g_ui.log_top + MICROPYTHON_LOG_VISIBLE_LINES >= maximum_top)
        {
            g_ui.log_top = maximum_top;
            g_ui.log_follow = 1U;
        }
        else
            g_ui.log_top += MICROPYTHON_LOG_VISIBLE_LINES;
        request_info_render();
        request_body_render();
    }
    if((pressed & BUTTON_OK) && !g_opened_from_pin &&
       !micropython_capability_bridge_display_presented() &&
       !micropython_capability_bridge_camera_presented())
    {
        if(runtime_active(runtime_state))
            request_runtime_stop();
        return;
    }
    if(pressed & BUTTON_BACK)
    {
        if(runtime_active(runtime_state))
        {
            g_close_after_stop = g_opened_from_pin;
            request_runtime_stop();
        }
        else if(g_opened_from_pin)
            g_close_requested = 1U;
        else
        {
            (void)refresh_file_list(1U, 0U);
            g_ui.selected_pinned = userfs_is_pinned(g_ui.selected_name);
            g_ui.mode = g_ui.file_count ? MICROPYTHON_UI_ACTIONS :
                                         MICROPYTHON_UI_LIST;
            set_status("");
            request_full_render();
        }
    }
}

void micropython_handle_buttons(const hk_input_event_t *input)
{
    micropython_runtime_status_t status;

    if(input->dropped || !(input->state & g_repeat_button) ||
       (input->pressed & (BUTTON_OK | BUTTON_BACK)))
        g_repeat_button = 0U;
    if(!input->dropped && !(input->pressed & (BUTTON_OK | BUTTON_BACK)) &&
       (input->pressed & (BUTTON_LEFT | BUTTON_RIGHT)) &&
       (g_ui.mode == MICROPYTHON_UI_LIST || g_ui.mode == MICROPYTHON_UI_PREVIEW))
    {
        g_repeat_button = input->pressed & BUTTON_LEFT ? BUTTON_LEFT : BUTTON_RIGHT;
        g_repeat_due_us = input->timestamp_us + MICROPYTHON_NAV_HOLD_US;
    }
    if(!input->pressed)
        return;
    micropython_runtime_get_status(&status);
    switch(g_ui.mode)
    {
    case MICROPYTHON_UI_ACTIONS:
        handle_action_buttons(input->pressed);
        break;
    case MICROPYTHON_UI_PREVIEW:
        handle_preview_buttons(input->pressed);
        break;
    case MICROPYTHON_UI_CONSOLE:
        handle_console_buttons(input->pressed, status.state);
        break;
    case MICROPYTHON_UI_DELETE_CONFIRM:
        if(input->pressed & BUTTON_BACK)
        {
            g_ui.mode = MICROPYTHON_UI_ACTIONS;
            set_status("");
            request_full_render();
        }
        else if(input->pressed & BUTTON_OK)
            delete_selected();
        break;
    default:
        handle_list_buttons(input->pressed, status.state);
        break;
    }
}

void micropython_poll_output(const hk_input_snapshot_t *input)
{
    char output[160];
    size_t count;
    micropython_runtime_status_t status;

    (void)input;
    micropython_runtime_poll();
    micropython_runtime_get_status(&status);
    if(status.state != g_last_state || status.exit_reason != g_last_exit)
    {
        g_last_state = status.state;
        g_last_exit = status.exit_reason;
        if(g_screen_active && g_ui.mode == MICROPYTHON_UI_CONSOLE)
        {
            request_info_render();
            request_body_render();
        }
    }
    if(status.run_id != g_observed_run_id)
    {
        g_observed_run_id = status.run_id;
        g_last_dropped = 0U;
        if(g_screen_active && runtime_active(status.state))
            show_console();
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
    char startup[USERFS_NAME_MAX + 1U];
    char line[192];

    read_startup(startup);
    micropython_runtime_get_status(&runtime);
    userfs_get_status(&filesystem);
    snprintf(line, sizeof(line),
             "HKMPSTATUS state=%u exit=%u run=%u source=%u pending=%u dropped=%u fs=%u fs_error=%u startup=%s\n",
             (unsigned)runtime.state, (unsigned)runtime.exit_reason,
             (unsigned)runtime.run_id, (unsigned)runtime.source_bytes,
             (unsigned)runtime.output_pending, (unsigned)runtime.output_dropped,
             (unsigned)filesystem.state, (unsigned)filesystem.last_error,
             startup[0] ? startup : "OFF");
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
        debug_console_write_text(run_debug_default() ? "HKMPRUN OK\n" :
                                                      "HKMPRUN ERROR\n");
        return 1U;
    }
    if(str_eq_ci(command, "HKMPTEST"))
    {
        uint8_t started;
        log_reset();
        started = micropython_runtime_start(
            g_stop_test_script, sizeof(g_stop_test_script) - 1U,
            MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS);
        if(started && g_screen_active)
            show_console();
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
        micropython_poll_output(NULL);
        uint32_t count = log_effective_count();
        for(uint32_t i = 0U; i < count; i++)
        {
            if(log_at(i)[0])
            {
                debug_console_write_text("HKMPLOG ");
                debug_console_write_text(log_at(i));
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
        if(g_screen_active && result == USERFS_OK)
        {
            g_ui.mode = MICROPYTHON_UI_LIST;
            (void)refresh_file_list(0U, 0U);
        }
        if(g_screen_active)
        {
            if(result != USERFS_OK)
                userfs_get_status(&g_filesystem);
            request_full_render();
        }
        return 1U;
    }
    return 0U;
}


static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t s_state_storage[1024];
static hk_result_t app_start(const hk_app_context_t *ctx)
{
    (void)ctx;
    micropython_enter(NULL);
    const char *name = micropython_program_launch_name();
    if(name)
    {
        g_opened_from_pin = 1U;
        run_file(name);
    }
    return HK_OK;
}
static hk_result_t app_event(const hk_app_context_t *ctx, const hk_app_event_t *event)
{
    if(event->kind == HK_APP_EVENT_INPUT)
        micropython_handle_buttons(&event->data.input);
    else if(event->kind == HK_APP_EVENT_TIMER)
    {
        micropython_poll_output(NULL);
        micropython_tick(event->timestamp_us);
    }
    return g_close_requested ? hk_app_context_request_close(ctx) : HK_OK;
}
static hk_result_t app_stop(const hk_app_context_t *ctx)
{
    hk_deadline_t deadline;
    hk_result_t result = hk_app_context_teardown_deadline(ctx, &deadline);
    micropython_exit();
    return result;
}
const hk_app_v2_entry_t micropython_v2_entry = {
    .state_storage = s_state_storage,
    .state_capacity_bytes = sizeof(s_state_storage),
    .start = app_start, .event = app_event, .render = NULL, .stop = app_stop,
};
