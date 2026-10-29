#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/ui/camera_view.h"
#include "../firmware/src/ui/display_binding.h"
#include "../firmware/src/core/hk_menu.h"
#include "../firmware/src/apps/apriltag/apriltag_view.h"
#include "../firmware/src/apps/face_detect/face_detect_view.h"
#include "../firmware/src/apps/object_detect/object_detect_view.h"

static uint8_t screen[240 * 240 * 2];
static uint16_t source[320 * 240];
static unsigned chrome_count, item_count, last_page, last_pages;
static camera_view_rect_t drawn_rect;
static const hk_app_t app = {.title = "APP"};
const hk_app_t *const g_menu_items[12] = {
    &app, &app, &app, &app, &app, &app, &app, &app, &app, &app, &app, &app
};
const uint8_t g_menu_item_count = 12;
void hk_back_exit_set_armed(uint8_t armed) { (void)armed; }
void hk_screen_set(screen_t value) { (void)value; }
screen_t hk_screen_get(void) { return SCREEN_MENU; }
void activity_note(void) {}
const hk_app_t *hk_app_for_id(const char *id) { (void)id; return &app; }
static void chrome(const char *title) { assert(title); chrome_count++; item_count = 0; }
static void title(const char *text) { assert(text); }
static void item(uint8_t slot, const hk_app_t *entry, uint8_t selected)
{ assert(slot < 9 && entry == &app); (void)selected; item_count++; }
static void page(uint8_t index, uint8_t count) { last_page = index; last_pages = count; }
const uint8_t *hk_font_glyph(uint32_t cp)
{ static const uint8_t glyph[48] = {0x80}; (void)cp; return glyph; }
uint8_t hk_ui_display_frame_acquire(hk_ui_display_surface_t *surface)
{ *surface = (hk_ui_display_surface_t){screen, 240, 240, 480, 1}; return 1; }
void hk_ui_display_frame_cancel(uint32_t id) { (void)id; }
uint8_t hk_ui_display_frame_present(uint32_t id) { return id == 1; }
void hk_ui_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{ (void)x; (void)y; (void)w; (void)h; (void)c; }
void hk_ui_display_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t t, uint16_t c)
{ drawn_rect = (camera_view_rect_t){x, y, w, h}; (void)t; (void)c; }
static uint16_t pixel(unsigned x, unsigned y)
{ unsigned at = (y * 240 + x) * 2; return ((uint16_t)screen[at] << 8) | screen[at + 1]; }

int main(void)
{
    const hk_menu_view_t view = {chrome, title, item, page};
    camera_view_frame_t frame = {.pixels = source, .width = 320, .height = 240};
    camera_view_present_t present;
    const camera_view_rect_t box = {160, 120, 80, 60};
    for(unsigned y = 0; y < 240; y++)
        for(unsigned x = 0; x < 320; x++) source[y * 320 + x] = x + 1;
    assert(camera_view_compose_frame(&frame, &present));
    assert(pixel(0, 29) == 0 && pixel(0, 30) == 1);
    assert(pixel(239, 209) == 319 && pixel(239, 210) == 0);
    camera_view_compose_rects(&present, &frame, &box, 1, 0x07e0);
    assert(pixel(120, 120) == 0x07e0 && pixel(179, 164) == 0x07e0);
    frame.rotate = 1;
    assert(camera_view_compose_frame(&frame, &present));
    camera_view_compose_rects(&present, &frame, &box, 1, 0x07e0);
    assert(pixel(75, 120) == 0x07e0);

    /* Four distinct corners catch a reversed rotation or an accidental flip. */
    for(unsigned y = 0; y < 240; y++)
        for(unsigned x = 0; x < 320; x++)
            source[y * 320 + x] = y < 120 ?
                (x < 160 ? 0xf800 : 0x07e0) : (x < 160 ? 0x001f : 0xffff);
    assert(camera_view_compose_frame(&frame, &present));
    assert(pixel(30, 0) == 0x001f && pixel(209, 0) == 0xf800);
    assert(pixel(30, 239) == 0xffff && pixel(209, 239) == 0x07e0);
    assert(pixel(29, 120) == 0 && pixel(210, 120) == 0);

    /* Feature views must use the same transform for boxes and text. */
    for(unsigned i = 0; i < 320 * 240; i++) source[i] = 0x001f;
    assert(camera_view_compose_frame(&frame, &present));
    const apriltag_result_t tag = {
        .id = 7, .x = 80, .y = 40, .w = 80, .h = 40,
        .center_x = 120, .center_y = 60,
    };
    const uint8_t selected = 1;
    apriltag_view_compose_results(&present, 320, 240, &tag, &selected, 1);
    assert(pixel(150, 60) == 0xffe0 && pixel(179, 119) == 0xffe0);
    assert(pixel(157, 78) == 0xffe0); /* centered ID in display coordinates */
    assert(pixel(124, 120) == 0xffff); /* crosshair at display center */

    assert(camera_view_compose_frame(&frame, &present));
    const object_detect_result_t object = {
        .class_id = 14, .confidence = 900, .x = 80, .y = 40, .w = 80, .h = 40,
    };
    object_detect_view_compose_results(&present, 320, 240, &object, 1);
    assert(pixel(179, 119) == 0xffe0);
    assert(pixel(100, 60) == 0xffe0); /* "person 90%", clamped to display width */

    const face_detect_box_t face = {.x = 80, .y = 40, .w = 80, .h = 40};
    face_detect_view_draw_boxes(320, 240, &face, 1);
    assert(drawn_rect.x == 150 && drawn_rect.y == 60);
    assert(drawn_rect.w == 30 && drawn_rect.h == 60);
    menu_view_set(&view);
    menu_render();
    assert(item_count == 9 && last_page == 0 && last_pages == 2);
    for(unsigned index = 0; index < 9; index++) menu_select_delta(1);
    assert(hk_menu_index_get() == 9 && chrome_count == 2 && item_count == 3 && last_page == 1);
    for(unsigned index = 0; index < 3; index++) menu_select_delta(1);
    assert(hk_menu_index_get() == 0 && chrome_count == 3 && item_count == 9 && last_page == 0);
    puts("CUBE_LAYOUT_OK preview=180x240 rotation=clockwise overlays=mapped menu=two-pages");
    return 0;
}
