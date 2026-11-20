#include <assert.h>
#include <stdio.h>

#include "../firmware/src/config/input_config.h"
#include "../firmware/src/config/menu_layout.h"
#include "../firmware/src/core/hk_dispatch.h"
#include "../firmware/src/core/hk_menu_runtime.h"

static const hk_app_t app = {.id = "fixture", .title = "APP"};
const hk_app_t *const g_menu_items[] = {
    &app, &app, &app, &app, &app, &app, &app, &app, &app, &app, &app, &app, &app
};
const uint8_t g_menu_item_count = sizeof(g_menu_items) / sizeof(g_menu_items[0]);
static unsigned opens;
static uint8_t current_page;

void hk_back_exit_set_armed(uint8_t armed) { (void)armed; }
void hk_screen_set(screen_t screen) { (void)screen; }
screen_t hk_screen_get(void) { return SCREEN_MENU; }
void activity_note(void) {}
const hk_app_t *hk_app_for_id(const char *id) { (void)id; return &app; }

static uint8_t mock_open(const hk_app_t *entry, const hk_input_snapshot_t *input)
{
    (void)entry;
    (void)input;
    opens++;
    return 0U;
}

static uint8_t pins(void) { return MENU_GRID_COLS == 3 ? 3U : 1U; }
static const char *pin(uint8_t index)
{
    static const char *const names[] = {"A.py", "B.py", "C.py"};
    return names[index];
}

static void page(uint8_t index, uint8_t count)
{
    assert(count == 2U);
    current_page = index;
}

static void press(uint32_t state, uint32_t pressed)
{
    const hk_input_snapshot_t input = {.state = state, .pressed = pressed};
    shell_handle_buttons(&input);
}

static void walk_column(const uint8_t *indices, size_t length)
{
    assert(hk_menu_index_get() == indices[0]);
    for(size_t i = 1; i < length; i++)
    {
        press(BUTTON_BACK, BUTTON_BACK);
        press(0U, 0U);
        assert(hk_menu_index_get() == indices[i]);
        assert(current_page == indices[i] / MENU_PAGE_ITEMS);
    }
}

int main(void)
{
    const hk_menu_owner_hooks_t hooks = {.enter = mock_open, .shortcut_count = pins,
                                        .shortcut_name = pin};
    const hk_menu_view_t view = {.draw_page = page};
#if MENU_GRID_COLS == 3
    const uint8_t first[] = {0, 3, 6, 9, 12, 15, 0};
    const uint8_t last[] = {2, 5, 8, 11, 14, 15, 2};
#else
    const uint8_t first[] = {0, 4, 8, 12, 0};
    const uint8_t third[] = {2, 6, 10, 13, 2};
    const uint8_t last[] = {3, 7, 11, 13, 3};
#endif
    menu_owner_hooks_set(&hooks);
    menu_view_set(&view);
    menu_render();
    walk_column(first, sizeof(first));
    for(uint8_t column = 1U; column < MENU_GRID_COLS; column++)
        press(BUTTON_RIGHT, BUTTON_RIGHT);
    walk_column(last, sizeof(last));
#if MENU_GRID_COLS == 4
    press(BUTTON_LEFT, BUTTON_LEFT);
    walk_column(third, sizeof(third));
    press(BUTTON_RIGHT, BUTTON_RIGHT);
#endif
    walk_column(last, sizeof(last) - 1U);
    press(BUTTON_RIGHT, BUTTON_RIGHT);
    assert(hk_menu_index_get() == 0U);
    press(BUTTON_BACK, BUTTON_BACK);
    assert(hk_menu_index_get() == MENU_GRID_COLS);
    press(BUTTON_LEFT, BUTTON_LEFT);

    press(BUTTON_BACK | BUTTON_OK, BUTTON_BACK | BUTTON_OK);
    assert(hk_menu_index_get() == 2U * MENU_GRID_COLS - 1U && opens == 0U);
    press(BUTTON_BACK | BUTTON_RIGHT, BUTTON_RIGHT);
    const hk_input_snapshot_t held = {.state = BUTTON_BACK | BUTTON_RIGHT};
    for(unsigned int tick = 0; tick < 70U; tick++)
        menu_tick(&held);
    assert(hk_menu_index_get() == 2U * MENU_GRID_COLS - 1U && opens == 0U);
    press(0U, 0U);
    press(BUTTON_LEFT, BUTTON_LEFT);
    assert(hk_menu_index_get() == 2U * MENU_GRID_COLS - 2U);
    press(BUTTON_OK, BUTTON_OK);
    assert(opens == 1U);
    puts("MENU_BACK_OK");
    return 0;
}
