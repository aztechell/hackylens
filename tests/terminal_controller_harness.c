#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../firmware/src/apps/terminal/terminal_controller.h"
#include "../firmware/src/apps/terminal/terminal_buffer.h"
#include "../firmware/src/apps/terminal/terminal_config.h"
#include "../firmware/src/core/hk_log.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("FAIL line=%d: %s\n", __LINE__, #condition);             \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static uint8_t s_flags;
static uint8_t s_mark_dirty;
static hk_log_sink_t s_sink;
static char s_log[512];

uint8_t settings_feature_flags(void)
{
    return s_flags;
}

void settings_set_feature_flags(uint8_t flags)
{
    s_flags = flags;
}

void settings_mark_dirty(uint8_t immediate)
{
    s_mark_dirty = immediate;
}

void shell_log_set_sink(hk_log_sink_t sink)
{
    s_sink = sink;
}

void shell_printf(const char *fmt, ...)
{
    va_list args;
    size_t used = strlen(s_log);

    va_start(args, fmt);
    (void)vsnprintf(s_log + used, sizeof(s_log) - used, fmt, args);
    va_end(args);
    if(s_sink)
        s_sink(s_log);
}

terminal_geometry_t terminal_view_geometry(terminal_font_size_t font_size)
{
    uint16_t scale = font_size == TERMINAL_FONT_SMALL ?
        TERMINAL_SMALL_SCALE : TERMINAL_NORMAL_SCALE;

    return (terminal_geometry_t){
        (uint16_t)(320U / (14U / scale)),
        (uint16_t)(240U / (24U / scale)),
        (uint16_t)(14U / scale),
        (uint16_t)(24U / scale),
    };
}

static hk_input_event_t press(uint32_t buttons)
{
    hk_input_event_t event = {0};

    event.state = buttons;
    event.changed = buttons;
    event.pressed = buttons;
    return event;
}

static hk_input_event_t release(uint32_t previous, uint32_t released)
{
    hk_input_event_t event = {0};

    event.state = previous & ~released;
    event.changed = released;
    event.pressed = 0U;
    event.released = released;
    return event;
}

int main(void)
{
    terminal_state_t state;
    hk_input_event_t event;
    unsigned tick;

    s_flags = 0U;
    s_mark_dirty = 0U;
    s_sink = NULL;
    s_log[0] = '\0';
    memset(&state, 0, sizeof(state));
    terminal_controller_reset(&state);
    CHECK(s_sink != NULL);
    CHECK(state.dirty == 1U);
    CHECK(state.font_size == TERMINAL_FONT_NORMAL);
    CHECK(strstr(s_log, "[BOOT] HackyLens LCD terminal") != NULL);

    event = press(HK_INPUT_BUTTON_LEFT);
    terminal_controller_handle_input(&state, &event);
    CHECK(state.repeat_button == HK_INPUT_BUTTON_LEFT);
    CHECK(state.dirty == 1U);

    event = press(HK_INPUT_BUTTON_BACK);
    terminal_controller_handle_input(&state, &event);
    CHECK(state.close_requested == 1U);

    terminal_controller_exit(&state);
    CHECK(s_sink == NULL);

    s_log[0] = '\0';
    memset(&state, 0, sizeof(state));
    terminal_controller_reset(&state);
    CHECK(strstr(s_log, "[TERM] opened") != NULL);

    event = press(HK_INPUT_BUTTON_OK);
    terminal_controller_handle_input(&state, &event);
    for(tick = 0U; tick < TERMINAL_OK_HOLD_TICKS; tick++)
        terminal_controller_tick(&state, HK_INPUT_BUTTON_OK);
    CHECK(state.font_size == TERMINAL_FONT_SMALL);
    CHECK(s_flags == TERMINAL_SETTINGS_FONT_SMALL_FLAG);
    CHECK(s_mark_dirty == 1U);

    event = release(HK_INPUT_BUTTON_OK, HK_INPUT_BUTTON_OK);
    terminal_controller_handle_input(&state, &event);
    CHECK(state.ok_active == 0U);

    printf("TERMINAL_CONTROLLER_OK back=1 reopen=1 font_hold=1\n");
    return 0;
}
