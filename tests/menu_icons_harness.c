#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hk_app.h"
#include "hk_ui.h"
#include "menu_layout.h"
#include "menu_icons.h"

#define STRIDE (HK_UI_DISPLAY_WIDTH * 2U + 16U)
#define FRAME_BYTES (STRIDE * HK_UI_DISPLAY_HEIGHT)

static uint8_t storage[FRAME_BYTES + 32U];
static uint8_t *const pixels = storage + 16U;
static unsigned acquires, presents, cancels;
static uint8_t active, fail_acquire, fail_present;
static hk_ui_display_rect_t dirty;

typedef void (*draw_icon_t)(uint16_t, uint16_t, uint16_t, uint16_t);
#define DECLARE_ICON(name) void name(uint16_t, uint16_t, uint16_t, uint16_t)
DECLARE_ICON(terminal_draw_icon);
DECLARE_ICON(camera_view_draw_icon);
DECLARE_ICON(qr_camera_draw_icon);
DECLARE_ICON(face_detect_view_draw_icon);
DECLARE_ICON(apriltag_view_draw_icon);
DECLARE_ICON(object_detect_view_draw_icon);
DECLARE_ICON(files_draw_icon);
DECLARE_ICON(buttons_draw_icon);
DECLARE_ICON(pong_draw_icon);
DECLARE_ICON(settings_draw_icon);
DECLARE_ICON(power_draw_icon);
DECLARE_ICON(micropython_view_draw_icon);

static const draw_icon_t callbacks[] = {
    terminal_draw_icon, camera_view_draw_icon, qr_camera_draw_icon,
    face_detect_view_draw_icon, apriltag_view_draw_icon, object_detect_view_draw_icon,
    files_draw_icon, buttons_draw_icon, pong_draw_icon, settings_draw_icon,
    power_draw_icon, micropython_view_draw_icon,
};

static void reset(void)
{
    memset(storage, 0x5a, sizeof(storage));
    acquires = presents = cancels = 0U;
    active = fail_acquire = fail_present = 0U;
    memset(&dirty, 0, sizeof(dirty));
}

static void guards(void)
{
    for(unsigned index = 0U; index < 16U; index++)
    {
        assert(storage[index] == 0x5a);
        assert(storage[FRAME_BYTES + 16U + index] == 0x5a);
    }
    for(unsigned y = 0U; y < HK_UI_DISPLAY_HEIGHT; y++)
        for(unsigned x = HK_UI_DISPLAY_WIDTH * 2U; x < STRIDE; x++)
            assert(pixels[y * STRIDE + x] == 0x5a);
}

uint8_t hk_ui_display_frame_acquire(hk_ui_display_surface_t *surface)
{
    acquires++;
    if(fail_acquire)
        return 0U;
    assert(!active);
    active = 1U;
    *surface = (hk_ui_display_surface_t){
        pixels, HK_UI_DISPLAY_WIDTH, HK_UI_DISPLAY_HEIGHT, STRIDE, 19U,
    };
    return 1U;
}

uint8_t hk_ui_display_frame_present_regions(
    uint32_t lease_id, const hk_ui_display_rect_t *regions, uint16_t count)
{
    assert(active && lease_id == 19U && count == 1U);
    dirty = regions[0];
    active = 0U;
    presents++;
    return fail_present ? 0U : 1U;
}

void hk_ui_display_frame_cancel(uint32_t lease_id)
{
    assert(lease_id == 19U);
    if(active)
    {
        active = 0U;
        cancels++;
    }
}

/* These view entry points are linked, but launcher icons must never use them. */
uint8_t hk_ui_display_frame_present(uint32_t lease_id)
{
    (void)lease_id;
    assert(0 && "icon must present only its dirty rectangle");
    return 0U;
}

uint8_t *hk_ui_display_row_buffer(void)
{
    assert(0 && "icon must use its acquired surface");
    return NULL;
}

void hk_ui_display_write_row(uint16_t x, uint16_t y, uint16_t width, const uint8_t *row)
{
    (void)x; (void)y; (void)width; (void)row;
    assert(0 && "icon must use its acquired surface");
}

void hk_ui_display_draw_text_at(
    uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    (void)x; (void)y; (void)text; (void)fg; (void)bg;
    assert(0 && "launcher artwork must not depend on text rendering");
}

void hk_ui_display_draw_text_centered(
    uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    (void)y; (void)text; (void)fg; (void)bg;
    assert(0 && "launcher artwork must not depend on text rendering");
}

void hk_ui_display_fill_rect(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    assert((uint32_t)x + width <= HK_UI_DISPLAY_WIDTH);
    assert((uint32_t)y + height <= HK_UI_DISPLAY_HEIGHT);
    for(unsigned row = y; row < (unsigned)y + height; row++)
        for(unsigned column = x; column < (unsigned)x + width; column++)
        {
            uint8_t *out = pixels + row * STRIDE + column * 2U;
            out[0] = (uint8_t)(color >> 8);
            out[1] = (uint8_t)color;
        }
}

void hk_ui_display_draw_rect(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    uint16_t thickness, uint16_t color)
{
    for(unsigned inset = 0U; inset < thickness; inset++)
    {
        if(width <= 2U * inset || height <= 2U * inset)
            break;
        hk_ui_display_fill_rect(x + inset, y + inset, width - 2U * inset, 1U, color);
        hk_ui_display_fill_rect(x + inset, y + height - 1U - inset, width - 2U * inset, 1U, color);
        hk_ui_display_fill_rect(x + inset, y + inset, 1U, height - 2U * inset, color);
        hk_ui_display_fill_rect(x + width - 1U - inset, y + inset, 1U, height - 2U * inset, color);
    }
}

static void save(const char *directory, unsigned icon, unsigned selected)
{
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/icon-%u-%u.rgb565", directory, icon, selected);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(pixels, 1U, FRAME_BYTES, file) == FRAME_BYTES);
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    for(unsigned icon = 0U; icon < sizeof(callbacks) / sizeof(callbacks[0]); icon++)
        for(unsigned selected = 0U; selected < 2U; selected++)
        {
            reset();
            hk_app_t app = {.draw_icon = callbacks[icon]};
            unsigned slot = icon % MENU_PAGE_ITEMS;
            unsigned x = MENU_ICON_X0 + slot % MENU_GRID_COLS * (MENU_ICON + MENU_ICON_GAP_X);
            unsigned y = MENU_ICON_Y0 + slot / MENU_GRID_COLS * (MENU_ICON + MENU_ICON_GAP_Y);
            menu_draw_item_at((uint8_t)slot, &app, (uint8_t)selected);
            assert(acquires == 1U && presents == 1U && cancels == 0U && !active);
            assert(dirty.x == x + 6U && dirty.y == y + 6U);
            assert(dirty.width == 48U && dirty.height == 48U);
            guards();
            save(argv[1], icon, selected);
        }

    /* A failed acquire or an out-of-range tile must leave the whole frame alone. */
    for(unsigned icon = 0U; icon < sizeof(callbacks) / sizeof(callbacks[0]); icon++)
    {
        reset();
        fail_acquire = 1U;
        callbacks[icon](12U, 36U, 0x1234U, 0xabcdU);
        assert(acquires == 1U && presents == 0U && cancels == 0U && !active);
        for(unsigned i = 0U; i < sizeof(storage); i++)
            assert(storage[i] == 0x5a);
    }
    const uint16_t bad_positions[][2] = {
        {HK_UI_DISPLAY_WIDTH - 53U, 0U}, {0U, HK_UI_DISPLAY_HEIGHT - 53U},
        {UINT16_MAX, UINT16_MAX},
    };
    for(unsigned i = 0U; i < sizeof(bad_positions) / sizeof(bad_positions[0]); i++)
    {
        reset();
        hk_menu_icon_draw(bad_positions[i][0], bad_positions[i][1], 0x1234U, 0xabcdU, hk_menu_icon_pong);
        assert(acquires == 1U && presents == 0U && cancels == 1U && !active);
        for(unsigned j = 0U; j < sizeof(storage); j++)
            assert(storage[j] == 0x5a);
    }

    /* Check RGB565 byte order with colors that exercise both bytes. */
    reset();
    hk_menu_icon_draw(12U, 36U, 0x1234U, 0xabcdU, hk_menu_icon_pong);
    for(unsigned y = 0U; y < 48U; y++)
        for(unsigned x = 0U; x < 48U; x++)
        {
            unsigned bit = hk_menu_icon_pong[y * 6U + x / 8U] & (0x80U >> (x & 7U));
            unsigned color = bit ? 0x1234U : 0xabcdU;
            unsigned at = (42U + y) * STRIDE + (18U + x) * 2U;
            assert(pixels[at] == (color >> 8) && pixels[at + 1U] == (color & 255U));
        }
    guards();
    reset();
    fail_present = 1U;
    hk_menu_icon_draw(12U, 36U, 0x1234U, 0xabcdU, hk_menu_icon_pong);
    assert(acquires == 1U && presents == 1U && !active);
    guards();
    puts("MENU_ICONS_OK all-callbacks padded-stride bounds leases rgb565");
    return 0;
}
