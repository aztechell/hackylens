#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../firmware/src/apps/settings/settings_controller.h"
#include "../firmware/src/config/settings_menu_layout.h"
#include "../firmware/src/ui/settings_menu_view.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("FAIL line=%d: %s\n", __LINE__, #condition);             \
            return 1;                                                        \
        }                                                                    \
    } while(0)

enum
{
    ITEM_TOGGLE = 0,
    ITEM_RANGE,
    ITEM_CHOICE,
    ITEM_COUNT,
};

static int32_t s_values[ITEM_COUNT];
static unsigned s_commits;
static unsigned s_view_calls;

void settings_menu_view_open(const char *title)
{
    (void)title;
    s_view_calls++;
}

void settings_menu_view_clear_rows(void)
{
    s_view_calls++;
}

void settings_menu_view_draw_row(uint8_t slot,
                                 const char *title,
                                 const char *value,
                                 uint8_t selected,
                                 uint8_t editing)
{
    (void)slot;
    (void)title;
    (void)value;
    (void)selected;
    (void)editing;
    s_view_calls++;
}

static int32_t read_value(void *context, uint16_t id)
{
    (void)context;
    return id < ITEM_COUNT ? s_values[id] : 0;
}

static uint8_t write_value(void *context, uint16_t id, int32_t value)
{
    (void)context;
    if(id >= ITEM_COUNT || s_values[id] == value)
        return 0U;
    s_values[id] = value;
    return 1U;
}

static void committed(void *context, uint16_t id)
{
    (void)context;
    (void)id;
    s_commits++;
}

static const char *const s_choices[] = {"A", "B", "C"};

static const settings_menu_item_t s_items[] = {
    {
        .id = ITEM_TOGGLE,
        .title = "Toggle",
        .kind = SETTINGS_MENU_ITEM_TOGGLE,
    },
    {
        .id = ITEM_RANGE,
        .title = "Range",
        .kind = SETTINGS_MENU_ITEM_RANGE,
        .interaction = SETTINGS_MENU_EDIT_ON_OK,
        .minimum = 0,
        .maximum = 10,
        .step = 1,
    },
    {
        .id = ITEM_CHOICE,
        .title = "Choice",
        .kind = SETTINGS_MENU_ITEM_CHOICE,
        .interaction = SETTINGS_MENU_EDIT_ON_OK,
        .choices = s_choices,
        .choice_count = 3U,
        .flags = SETTINGS_MENU_ITEM_WRAP,
    },
};

static const settings_menu_definition_t s_definition = {
    .title = "SETTINGS",
    .items = s_items,
    .item_count = (uint8_t)ITEM_COUNT,
    .read = read_value,
    .write = write_value,
    .committed = committed,
};

static hk_input_event_t press(uint32_t buttons)
{
    hk_input_event_t event = {0};

    event.state = buttons;
    event.changed = buttons;
    event.pressed = buttons;
    return event;
}

static uint8_t selected_id(const settings_state_t *state)
{
    const char *title = NULL;
    char value[SETTINGS_MENU_VALUE_SIZE];
    uint8_t selected = 0U;
    uint8_t editing = 0U;
    uint8_t slot;

    for(slot = 0U; slot < SETTINGS_MENU_VISIBLE_ROWS; slot++)
    {
        if(settings_menu_visible_slot(
               &state->menu, slot, &title, value, sizeof(value),
               &selected, &editing) &&
           selected)
            return (uint8_t)(state->menu.top + slot);
    }
    return 0xFFU;
}

int main(void)
{
    settings_state_t state;
    hk_input_event_t event;
    const char *title = NULL;
    char value[SETTINGS_MENU_VALUE_SIZE];
    uint8_t selected = 0U;
    uint8_t editing = 0U;

    memset(&state, 0, sizeof(state));
    s_values[ITEM_TOGGLE] = 0;
    s_values[ITEM_RANGE] = 5;
    s_values[ITEM_CHOICE] = 0;
    s_commits = 0U;
    s_view_calls = 0U;
    settings_controller_reset(&state, &s_definition);
    CHECK(settings_menu_active(&state.menu) == 1U);
    CHECK(state.menu.headless == 1U);
    CHECK(s_view_calls == 0U);
    CHECK(settings_menu_visible_slot(
              &state.menu, 0U, &title, value, sizeof(value),
              &selected, &editing) == 1U);
    CHECK(strcmp(title, "Toggle") == 0);
    CHECK(strcmp(value, "OFF") == 0);
    CHECK(selected == 1U);

    event = press(HK_INPUT_BUTTON_OK);
    settings_controller_handle_input(&state, &event);
    CHECK(s_values[ITEM_TOGGLE] == 1);
    CHECK(s_view_calls == 0U);

    event = press(HK_INPUT_BUTTON_RIGHT);
    settings_controller_handle_input(&state, &event);
    CHECK(selected_id(&state) == ITEM_RANGE);

    event = press(HK_INPUT_BUTTON_OK);
    settings_controller_handle_input(&state, &event);
    CHECK(state.menu.editing == 1U);
    event = press(HK_INPUT_BUTTON_LEFT);
    settings_controller_handle_input(&state, &event);
    CHECK(s_values[ITEM_RANGE] == 4);
    event = press(HK_INPUT_BUTTON_OK);
    settings_controller_handle_input(&state, &event);
    CHECK(state.menu.editing == 0U);
    CHECK(s_commits == 1U);

    event = press(HK_INPUT_BUTTON_BACK);
    settings_controller_handle_input(&state, &event);
    CHECK(state.close_requested == 1U);

    settings_controller_exit(&state);
    CHECK(settings_menu_active(&state.menu) == 0U);

    memset(&state, 0, sizeof(state));
    settings_controller_reset(&state, &s_definition);
    CHECK(s_values[ITEM_TOGGLE] == 1);
    CHECK(settings_menu_visible_slot(
              &state.menu, 0U, &title, value, sizeof(value),
              &selected, &editing) == 1U);
    CHECK(strcmp(value, "ON") == 0);
    CHECK(s_view_calls == 0U);

    printf("SETTINGS_CONTROLLER_OK nav=1 change=1 save=1 reopen=1\n");
    return 0;
}
