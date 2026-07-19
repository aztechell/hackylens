#include "settings_model.h"

#include <stdio.h>

#include "../config/settings_config.h"
#include "../config/settings_layout.h"
#include "hk_config.h"

#include "../services/settings_service.h"
#include "../ui/settings_view.h"

static settings_view_row_t g_settings_rows[SETTINGS_ROW_COUNT];

const char *settings_model_row_title(uint8_t index)
{
    return settings_view_row_title(index);
}

static void settings_row_value(uint8_t index, uint8_t selected, uint8_t editing, char *value, size_t value_size)
{
    const char *edit_mark = editing && selected ? "*" : "";

    if(index == SETTINGS_LED_SWITCH)
        snprintf(value, value_size, "%s", settings_led_enabled() ? "ON" : "OFF");
    else if(index == SETTINGS_LED_BRIGHTNESS)
        snprintf(value, value_size, "%u%s", settings_led_brightness(), edit_mark);
    else if(index == SETTINGS_RGB_SWITCH)
        snprintf(value, value_size, "%s", settings_rgb_enabled() ? "ON" : "OFF");
    else if(index == SETTINGS_RGB_RED)
        snprintf(value, value_size, "%u%s", settings_rgb_red(), edit_mark);
    else if(index == SETTINGS_RGB_GREEN)
        snprintf(value, value_size, "%u%s", settings_rgb_green(), edit_mark);
    else if(index == SETTINGS_RGB_BLUE)
        snprintf(value, value_size, "%u%s", settings_rgb_blue(), edit_mark);
    else if(index == SETTINGS_SCREEN_BRIGHTNESS)
        snprintf(value, value_size, "%u%s", settings_screen_brightness(), edit_mark);
    else if(index == SETTINGS_AUTO_SLEEP)
        snprintf(value, value_size, "%umin%s", settings_auto_sleep_minutes(), edit_mark);
    else if(index == SETTINGS_EXTERNAL_LINK)
        snprintf(value, value_size, "%s%s",
                 settings_external_link_transport() == EXTERNAL_LINK_I2C ? "I2C" : "UART", edit_mark);
    else if(index == SETTINGS_UART_SPEED)
        snprintf(value, value_size, "%u%s", (unsigned)settings_external_link_uart_baud(), edit_mark);
    else
        snprintf(value, value_size, "v%s", HACKYLENS_VERSION);
}

static void settings_build_model(settings_view_model_t *model, uint8_t index, uint8_t top, uint8_t editing)
{
    for(uint8_t i = 0; i < SETTINGS_ROW_COUNT; i++)
    {
        g_settings_rows[i].title = settings_model_row_title(i);
        settings_row_value(i, index == i, editing, g_settings_rows[i].value, sizeof(g_settings_rows[i].value));
    }

    model->rows = g_settings_rows;
    model->row_count = SETTINGS_ROW_COUNT;
    model->index = index;
    model->top = top;
}

void settings_model_ensure_visible(uint8_t index, uint8_t *top)
{
    uint8_t next_top;

    if(!top)
        return;

    next_top = *top;
    if(index < next_top)
        next_top = index;
    else if(index >= next_top + SETTINGS_VISIBLE_ROWS)
        next_top = (uint8_t)(index - SETTINGS_VISIBLE_ROWS + 1U);

    if(next_top + SETTINGS_VISIBLE_ROWS > SETTINGS_ROW_COUNT)
        next_top = SETTINGS_ROW_COUNT > SETTINGS_VISIBLE_ROWS ? SETTINGS_ROW_COUNT - SETTINGS_VISIBLE_ROWS : 0;

    *top = next_top;
}

void settings_model_draw_row(uint8_t index, uint8_t top, uint8_t editing, uint8_t row_index)
{
    settings_view_model_t model;

    settings_build_model(&model, index, top, editing);
    settings_view_draw_row(&model, row_index);
}

void settings_model_render(uint8_t index, uint8_t top, uint8_t editing)
{
    settings_view_model_t model;

    settings_build_model(&model, index, top, editing);
    settings_view_render(&model);
}
