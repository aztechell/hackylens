#include "buttons_view.h"

#include <stdio.h>
#include <string.h>

#define BUTTONS_COLOR_BLACK 0x0000
#define BUTTONS_COLOR_WHITE 0xFFFF
#define BUTTONS_COLOR_GREEN 0x07E0
#define BUTTONS_BAR_H 30
#define BUTTONS_LINE 2
#define BUTTONS_ROW_H 32
#define BUTTONS_ROW0_Y 42

static const uint32_t s_button_masks[4] = {
    HK_INPUT_BUTTON_LEFT,
    HK_INPUT_BUTTON_OK,
    HK_INPUT_BUTTON_RIGHT,
    HK_INPUT_BUTTON_BACK,
};

static const char *const s_button_names[4] = {
    "LEFT", "OK", "RIGHT", "BACK",
};

static const char *buttons_view_status(
    const buttons_view_state_t *state, uint32_t mask)
{
    if(state->repeat_error & mask)
        return "REPEAT";
    if(state->state & mask)
        return (state->hold_passed & mask) ? "HOLD" : "PRESS";
    if(state->passed & mask)
        return "PASS";
    return "WAIT";
}

static hk_result_t draw_text(
    hk_app_surface_t *surface,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height,
    const char *text,
    uint16_t rgb565)
{
    hk_display_rect_t bounds = {x, y, width, height};

    return hk_app_surface_text(
        surface, &bounds, text, (uint32_t)strlen(text), rgb565);
}

hk_result_t buttons_view_render(
    hk_app_surface_t *surface, const buttons_view_state_t *state)
{
    hk_display_info_t info = {0};
    hk_display_rect_t bar;
    hk_display_rect_t rule;
    char line[48];
    uint8_t passed = 0U;
    uint8_t index;
    hk_result_t result;

    if(!surface || !state)
        return HK_ERR_INVALID_ARGUMENT;
    result = hk_app_surface_get_info(surface, &info);
    if(result != HK_OK)
        return result;
    result = hk_app_surface_clear(surface, BUTTONS_COLOR_BLACK);
    if(result != HK_OK)
        return result;
    bar = (hk_display_rect_t){0, 0, info.width, BUTTONS_BAR_H};
    result = hk_app_surface_stroke_rect(surface, &bar, BUTTONS_COLOR_GREEN);
    if(result != HK_OK)
        return result;
    rule = (hk_display_rect_t){
        0, BUTTONS_BAR_H - BUTTONS_LINE, info.width, BUTTONS_LINE,
    };
    result = hk_app_surface_fill_rect(surface, &rule, BUTTONS_COLOR_GREEN);
    if(result != HK_OK)
        return result;
    result = draw_text(
        surface, 12, 6, info.width - 24U, 16U, "BUTTON TEST",
        BUTTONS_COLOR_GREEN);
    if(result != HK_OK)
        return result;
    for(index = 0U; index < 4U; index++)
    {
        uint32_t mask = s_button_masks[index];
        uint16_t color = (state->repeat_error & mask) ?
            BUTTONS_COLOR_WHITE : BUTTONS_COLOR_GREEN;

        snprintf(
            line, sizeof(line), "%-5s %s P:%02u R:%02u %-6s",
            s_button_names[index],
            (state->state & mask) ? "DN" : "UP",
            (unsigned)state->pressed_count[index],
            (unsigned)state->released_count[index],
            buttons_view_status(state, mask));
        result = draw_text(
            surface, 12, BUTTONS_ROW0_Y + index * BUTTONS_ROW_H,
            info.width - 24U, 20U, line, color);
        if(result != HK_OK)
            return result;
        if(state->passed & mask)
            passed++;
    }
    snprintf(
        line, sizeof(line), "RESULT: %u/4%s", (unsigned)passed,
        state->repeat_error ? "  REPEAT ERROR" : "");
    result = draw_text(
        surface, 12, 184, info.width - 24U, 16U, line, BUTTONS_COLOR_GREEN);
    if(result != HK_OK)
        return result;
    return draw_text(
        surface, 12, 207, info.width - 24U, 16U, "HOLD OK+BACK 1S TO EXIT",
        BUTTONS_COLOR_GREEN);
}
