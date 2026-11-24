#include <hackylens/app/panel.h>
#include <inttypes.h>
#include <stdio.h>

#include "../../core/hk_string.h"
#include "../../core/hk_ui_colors.h"
#include "../../services/axp173_monitor.h"
#include "hackylens_font_1bpp.h"

typedef enum
{
    PAGE_BATTERY, PAGE_INPUTS, PAGE_CHARGER, PAGE_RAILS, PAGE_ADC,
    PAGE_COUNTERS, PAGE_IRQS, INFO_PAGE_COUNT,
} info_page_t;

static const char *const page_names[INFO_PAGE_COUNT] = {
    "BATTERY", "INPUTS", "CHARGER", "RAIL SET", "ADC", "COUNTERS", "IRQ EN/HIT",
};

#define RAW_ROWS 6U
#define RAW_PER_PAGE (RAW_ROWS * 2U)

typedef struct
{
    axp173_monitor_t monitor;
    uint32_t samples;
    uint8_t page;
    uint8_t raw;
} axp173_app_state_t;

static _Alignas(HK_APP_STATE_ALIGNMENT) uint8_t storage[1024];
_Static_assert(sizeof(axp173_app_state_t) <= sizeof(storage), "AXP173 app state too large");
static const axp173_app_state_t *active;

static axp173_app_state_t *app_state(const hk_app_context_t *context)
{
    void *state;
    uint32_t size;
    return hk_app_context_state(context, &state, &size) == HK_OK &&
           size >= sizeof(axp173_app_state_t) ? state : NULL;
}

static hk_result_t start_app(const hk_app_context_t *context)
{
    axp173_app_state_t *state = app_state(context);
    if(!state)
        return HK_ERR_INVALID_STATE;
    state->samples = 0U;
    state->page = 0U;
    state->raw = 0U;
    axp173_monitor_start(&state->monitor);
    active = state;
    return HK_OK;
}

static uint8_t raw_pages(void)
{
    return (axp173_register_count() + RAW_PER_PAGE - 1U) / RAW_PER_PAGE;
}

static hk_result_t event(const hk_app_context_t *context, const hk_app_event_t *input)
{
    axp173_app_state_t *state = app_state(context);
    if(!state)
        return HK_ERR_INVALID_STATE;
    if(input->kind == HK_APP_EVENT_TIMER)
    {
        if(!axp173_monitor_step(&state->monitor))
            return HK_OK;
        ++state->samples;
    }
    else if(input->kind == HK_APP_EVENT_INPUT)
    {
        const uint32_t pressed = input->data.input.pressed;
        if(pressed & HK_INPUT_BUTTON_BACK)
            return hk_app_context_request_close(context);
        if(pressed & HK_INPUT_BUTTON_OK)
        {
            state->raw ^= 1U;
            state->page = 0U;
        }
        else if(pressed & (HK_INPUT_BUTTON_LEFT | HK_INPUT_BUTTON_RIGHT))
        {
            const uint8_t count = state->raw ? raw_pages() : INFO_PAGE_COUNT;
            state->page = (state->page + (pressed & HK_INPUT_BUTTON_RIGHT ?
                           1U : count - 1U)) % count;
        }
        else
            return HK_OK;
    }
    else
        return HK_OK;
    return hk_app_context_request_render(context, NULL);
}

static void row(hk_app_panel_t *panel, uint8_t index, const char *label, const char *value)
{
    const uint16_t y = 62U + index * 25U;
    const uint16_t split = 6U * HACKYLENS_FONT_W + 6U;
    hk_app_panel_label(panel, 12U, y, split, label, hk_ui_accent(), HK_PANEL_LEFT);
    hk_app_panel_label(panel, 12U + split, y, panel->width - 24U - split,
                       value, hk_ui_accent(), HK_PANEL_RIGHT);
}

static void format_voltage(char *value, size_t size, unsigned mv)
{
    snprintf(value, size, "%u.%03uV", mv / 1000U, mv % 1000U);
}

static void measure_row(hk_app_panel_t *panel, uint8_t index, const char *label,
                        const axp173_snapshot_t *sample, axp173_measurement_t channel)
{
    char text[20] = "--";
    int32_t value;
    if(axp173_measurement(sample, channel, &value))
    {
        if(channel == AXP173_DIE_DECIC)
        {
            const unsigned magnitude = (unsigned)(value < 0 ? -value : value);
            snprintf(text, sizeof(text), "%s%u.%uC", value < 0 ? "-" : "",
                     magnitude / 10U, magnitude % 10U);
        }
        else if(channel == AXP173_BAT_UW)
            snprintf(text, sizeof(text), "%umW", (unsigned)(value + 500) / 1000U);
        else if(channel == AXP173_CHARGE_UA || channel == AXP173_DISCHARGE_UA ||
                channel == AXP173_USB_UA || channel == AXP173_AC_UA)
        {
            const unsigned tenths = (unsigned)(value + 50) / 100U;
            snprintf(text, sizeof(text), "%u.%umA", tenths / 10U, tenths % 10U);
        }
        else
            format_voltage(text, sizeof(text), (unsigned)(value + 500) / 1000U);
    }
    row(panel, index, label, text);
}

static void battery(hk_app_panel_t *panel, const axp173_snapshot_t *sample)
{
    uint8_t percent;
    char level[8] = "--";
    uint32_t input, charge;
    const char *state = "--";
    if(axp173_snapshot_read(sample, AXP173_CHARGE_STATUS, 1U, &charge))
    {
        if(!(charge & AXP173_BATTERY_PRESENT))
            state = "NO BAT";
        else if(charge & AXP173_CHARGING)
            state = "CHARGING";
        else if(axp173_snapshot_read(sample, AXP173_INPUT_STATUS, 1U, &input))
            state = input & (AXP173_USB_VALID | AXP173_AC_VALID) ? "IDLE" : "SUPPLY";
    }
    if(axp173_battery_estimate(sample, &percent))
        snprintf(level, sizeof(level), "~%u%%", (unsigned)percent);
    row(panel, 0U, "EST", level);
    measure_row(panel, 1U, "BAT V", sample, AXP173_BAT_UV);
    row(panel, 2U, "STATE", state);
    measure_row(panel, 3U, "CHG I", sample, AXP173_CHARGE_UA);
    measure_row(panel, 4U, "DIS I", sample, AXP173_DISCHARGE_UA);
    measure_row(panel, 5U, "BAT P", sample, AXP173_BAT_UW);
}

static void inputs(hk_app_panel_t *panel, const axp173_snapshot_t *sample)
{
    uint32_t status;
    const char *source = "--";
    if(axp173_snapshot_read(sample, AXP173_INPUT_STATUS, 1U, &status))
    {
        if(status & AXP173_AC_VALID)
            source = status & AXP173_USB_VALID ? "AC+USB" : "ACIN";
        else if(status & AXP173_USB_VALID)
            source = "USB";
        else
            source = status & (AXP173_USB_PRESENT | AXP173_AC_PRESENT) ? "INVALID" : "BAT";
    }
    row(panel, 0U, "VALID", source);
    measure_row(panel, 1U, "USB V", sample, AXP173_USB_UV);
    measure_row(panel, 2U, "USB I", sample, AXP173_USB_UA);
    measure_row(panel, 3U, "AC V", sample, AXP173_AC_UV);
    measure_row(panel, 4U, "AC I", sample, AXP173_AC_UA);
    measure_row(panel, 5U, "APS V", sample, AXP173_SYS_UV);
}

static void charger(hk_app_panel_t *panel, const axp173_snapshot_t *sample)
{
    static const uint16_t targets[] = {4100, 4150, 4200, 4360};
    static const uint16_t currents[] = {
        100, 190, 280, 360, 450, 550, 630, 700, 780, 880, 960, 1000,
        1080, 1160, 1240, 1320,
    };
    uint32_t config, timers;
    char text[14];
    if(axp173_snapshot_read(sample, AXP173_CHARGER_CONFIG, 1U, &config))
    {
        row(panel, 0U, "ENABLE", config & 0x80U ? "ON" : "OFF");
        format_voltage(text, sizeof(text), targets[(config >> 5U) & 3U]);
        row(panel, 1U, "TARGET", text);
        snprintf(text, sizeof(text), "%umA", (unsigned)currents[config & 15U]);
        row(panel, 2U, "SET I", text);
        row(panel, 3U, "END I", config & 0x10U ? "15%" : "10%");
    }
    else
    {
        row(panel, 0U, "ENABLE", "--");
        row(panel, 1U, "TARGET", "--");
        row(panel, 2U, "SET I", "--");
        row(panel, 3U, "END I", "--");
    }
    if(axp173_snapshot_read(sample, AXP173_CHARGER_TIMERS, 1U, &timers))
    {
        snprintf(text, sizeof(text), "%umin", (unsigned)(30U + 10U * (timers >> 6U)));
        row(panel, 4U, "PRE", text);
        snprintf(text, sizeof(text), "%uh", (unsigned)(7U + (timers & 3U)));
        row(panel, 5U, "CC", text);
    }
    else
    {
        row(panel, 4U, "PRE", "--");
        row(panel, 5U, "CC", "--");
    }
}

static void rails(hk_app_panel_t *panel, const axp173_snapshot_t *sample)
{
    static const struct
    {
        const char *label;
        uint8_t enable, reg, shift, mask;
        uint16_t base, step;
    } outputs[] = {
        {"DC1",  0x01, 0x26, 0, 0x7f, 700, 25},
        {"DC2",  0x10, 0x23, 0, 0x3f, 700, 25},
        {"LDO2", 0x04, 0x28, 4, 0x0f, 1800, 100},
        {"LDO3", 0x08, 0x28, 0, 0x0f, 1800, 100},
        {"LDO4", 0x02, 0x27, 0, 0x7f, 700, 25},
    };
    uint32_t enable;
    const uint8_t have_enable = axp173_snapshot_read(sample, AXP173_OUTPUT_CONTROL,
                                                     1U, &enable);
    for(uint8_t i = 0U; i < sizeof(outputs) / sizeof(outputs[0]); i++)
    {
        char text[12] = "--";
        uint32_t voltage;
        if(have_enable && !(enable & outputs[i].enable))
            snprintf(text, sizeof(text), "OFF");
        else if(have_enable && axp173_snapshot_read(sample, outputs[i].reg, 1U, &voltage))
        {
            const unsigned code = (voltage >> outputs[i].shift) & outputs[i].mask;
            const unsigned mv = outputs[i].base + code * outputs[i].step;
            if(mv <= 3500U)
                format_voltage(text, sizeof(text), mv);
            else
                snprintf(text, sizeof(text), "0x%02X", code);
        }
        row(panel, i, outputs[i].label, text);
    }
    row(panel, 5U, "LDO1", have_enable ? "ON" : "--");
}

static void adc(hk_app_panel_t *panel, const axp173_snapshot_t *sample)
{
    uint32_t value;
    char text[15];
    measure_row(panel, 0U, "DIE", sample, AXP173_DIE_DECIC);
    measure_row(panel, 1U, "TS", sample, AXP173_TS_UV);
    row(panel, 2U, "HOT", !axp173_snapshot_read(sample, AXP173_CHARGE_STATUS, 1U, &value) ?
                         "--" : value & AXP173_DIE_HOT ? "YES" : "NO");
    for(uint8_t i = 0U; i < 2U; i++)
    {
        const char *name = i ? "ADC2" : "ADC1";
        if(axp173_snapshot_read(sample, AXP173_ADC_ENABLE1 + i, 1U, &value))
        {
            snprintf(text, sizeof(text), "0x%02" PRIX32, value);
            row(panel, 3U + i, name, text);
        }
        else
            row(panel, 3U + i, name, "--");
    }
    if(axp173_snapshot_read(sample, AXP173_ADC_CONFIG, 1U, &value))
    {
        snprintf(text, sizeof(text), "%uHz", 25U << (value >> 6U));
        row(panel, 5U, "RATE", text);
    }
    else
        row(panel, 5U, "RATE", "--");
}

static void coulomb_counters(hk_app_panel_t *panel, const axp173_snapshot_t *sample)
{
    uint32_t value;
    row(panel, 0U, "COUNT", !axp173_snapshot_read(sample, AXP173_COULOMB_CONTROL, 1U, &value) ?
                           "--" : value & 0x80U ? "ON" : "OFF");
    for(uint8_t i = 0U; i < 2U; i++)
    {
        char text[12] = "--";
        if(axp173_snapshot_read(sample, 0xb0U + 4U * i, 4U, &value))
            snprintf(text, sizeof(text), "%08" PRIX32, value);
        row(panel, 1U + i, i ? "OUT" : "IN", text);
    }
}

static void irqs(hk_app_panel_t *panel, const axp173_snapshot_t *sample)
{
    for(uint8_t i = 0U; i < 5U; i++)
    {
        uint32_t enable, status;
        char name[6], text[8] = "--";
        snprintf(name, sizeof(name), "IRQ%u", (unsigned)i + 1U);
        if(axp173_snapshot_read(sample, i < 4U ? 0x40U + i : 0x4aU, 1U, &enable) &&
           axp173_snapshot_read(sample, i < 4U ? 0x44U + i : 0x4dU, 1U, &status))
            snprintf(text, sizeof(text), "%02X/%02X", (unsigned)enable, (unsigned)status);
        row(panel, i, name, text);
    }
}

static void draw_raw(hk_app_panel_t *panel, const axp173_app_state_t *state)
{
    for(uint8_t row_index = 0U; row_index < RAW_ROWS; row_index++)
    {
        char pairs[2][8] = {{0}};
        for(uint8_t column = 0U; column < 2U; column++)
        {
            const uint8_t reg = axp173_register_address(state->page * RAW_PER_PAGE +
                                                        row_index * 2U + column);
            uint32_t value;
            if(reg == UINT8_MAX)
                break;
            if(axp173_snapshot_read(&state->monitor.sample, reg, 1U, &value))
                snprintf(pairs[column], sizeof(pairs[column]), "%02X:%02X",
                         (unsigned)reg, (unsigned)value);
            else
                snprintf(pairs[column], sizeof(pairs[column]), "%02X:--", (unsigned)reg);
        }
        row(panel, row_index, pairs[0], pairs[1]);
    }
}

static hk_result_t render(const hk_app_context_t *context, hk_app_surface_t *surface)
{
    axp173_app_state_t *state = app_state(context);
    if(!state)
        return HK_ERR_INVALID_STATE;
    hk_app_panel_t panel = hk_app_panel_begin(surface);
    hk_app_panel_chrome(&panel, "AXP173");
    char title[24];
    if(state->raw)
        snprintf(title, sizeof(title), "< REG %u/%u >", (unsigned)state->page + 1U,
                 (unsigned)raw_pages());
    else
        snprintf(title, sizeof(title), "< %s >", page_names[state->page]);
    hk_app_panel_status(&panel, 34U, title);

    const axp173_snapshot_t *sample = &state->monitor.sample;
    if(!state->samples || !sample->count)
        hk_app_panel_status(&panel, 111U, state->samples ? "I2C ERROR" : "READING");
    else if(state->raw)
        draw_raw(&panel, state);
    else
    {
        switch(state->page)
        {
        case PAGE_BATTERY: battery(&panel, sample); break;
        case PAGE_INPUTS: inputs(&panel, sample); break;
        case PAGE_CHARGER: charger(&panel, sample); break;
        case PAGE_RAILS: rails(&panel, sample); break;
        case PAGE_ADC: adc(&panel, sample); break;
        case PAGE_COUNTERS: coulomb_counters(&panel, sample); break;
        case PAGE_IRQS: irqs(&panel, sample); break;
        }
    }
    hk_app_panel_status(&panel, 213U, state->raw ? "OK INFO" : "OK REG");
    return panel.result;
}

static hk_result_t stop(const hk_app_context_t *context)
{
    (void)context;
    active = NULL;
    printf("HKAXP STOP adc=%s\r\n", axp173_monitor_stop() ? "RESTORED" : "RETRY");
    return HK_OK;
}

uint8_t axp173_handle_debug_command(const char *command)
{
    const uint8_t dump = str_eq_ci(command, "HKAXPREGS");
    if(!dump && !str_eq_ci(command, "HKAXPINFO"))
        return 0U;
    if(!active)
        puts("HKAXPINFO active=0");
    else
    {
        const axp173_snapshot_t *sample = &active->monitor.sample;
        const uint8_t total = axp173_register_count();
        printf("HKAXPINFO active=1 raw=%u page=%u valid=%u total=%u samples=%" PRIu32 "\r\n",
               (unsigned)active->raw, (unsigned)active->page, (unsigned)sample->count,
               (unsigned)total, active->samples);
        if(dump)
        {
            for(uint8_t index = 0U; index < total; index++)
            {
                const uint8_t reg = axp173_register_address(index);
                uint32_t value;
                if(axp173_snapshot_read(sample, reg, 1U, &value))
                    printf("HKAXPREG %02X %02X\r\n", (unsigned)reg, (unsigned)value);
                else
                    printf("HKAXPREG %02X --\r\n", (unsigned)reg);
            }
            puts("HKAXPREGS END");
        }
    }
    return 1U;
}

const hk_app_v2_entry_t axp173_v2_entry = {
    .state_storage = storage,
    .state_capacity_bytes = sizeof(storage),
    .start = start_app,
    .event = event,
    .render = render,
    .stop = stop,
};
