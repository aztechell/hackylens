#include "color_tracker.h"

#include <assert.h>
#include <stddef.h>

static hk_color_tracker_t tracker;
static uint16_t pixels[HK_COLOR_MAX_WIDTH * HK_COLOR_MAX_HEIGHT];
static const hk_color_threshold_t red = {31U, 31U, 0U, 0U, 0U, 0U};

static void expect_blob(hk_color_blob_t blob, uint32_t count,
                        uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    assert(blob.pixels == count);
    assert(blob.bounds.x == x && blob.bounds.y == y);
    assert(blob.bounds.width == w && blob.bounds.height == h);
}

static void test_bridge_and_rotation(void)
{
    const uint16_t image[] = {
        0xf800, 0, 0, 0xf800, 0, 0,
        0xf800, 0xf800, 0xf800, 0xf800, 0, 0xf800,
        0, 0, 0xf800, 0, 0, 0xf800,
    };
    hk_color_frame_t frame = {image, 6U, 3U, 0U};
    hk_color_blob_t blob;
    hk_color_rect_t roi = {4U, 0U, 2U, 3U};
    assert(hk_color_find_largest(&tracker, &frame, NULL, &red, 1U, &blob));
    expect_blob(blob, 7U, 0U, 0U, 4U, 3U);
    assert(hk_color_find_largest(&tracker, &frame, &roi, &red, 1U, &blob));
    expect_blob(blob, 2U, 5U, 1U, 1U, 2U);
    assert(hk_color_find_largest(&tracker, &frame, &roi, &red, 3U, &blob));
    expect_blob(blob, 0U, 0U, 0U, 0U, 0U);
    roi.width = 3U;
    assert(!hk_color_find_largest(&tracker, &frame, &roi, &red, 1U, &blob));
    const hk_color_threshold_t invalid = {31U, 30U, 0U, 0U, 0U, 0U};
    assert(!hk_color_find_largest(&tracker, &frame, NULL, &invalid, 1U, &blob));
    assert(!hk_color_find_largest(&tracker, &frame, NULL, &red, 0U, &blob));

    frame.rotate = 1U;
    roi = (hk_color_rect_t){0U, 4U, 3U, 2U};
    assert(hk_color_find_largest(&tracker, &frame, &roi, &red, 1U, &blob));
    expect_blob(blob, 2U, 0U, 5U, 2U, 1U);
}

static void test_maximum_frame(void)
{
    hk_color_frame_t frame = {pixels, HK_COLOR_MAX_WIDTH, HK_COLOR_MAX_HEIGHT, 0U};
    hk_color_blob_t blob;
    for(uint32_t y = 0U; y < frame.height; y++)
        for(uint32_t x = 0U; x < frame.width; x++)
            pixels[y * frame.width + x] = (x + y) & 1U ? 0xf800 : 0U;
    assert(hk_color_find_largest(&tracker, &frame, NULL, &red, 1U, &blob));
    expect_blob(blob, 1U, 1U, 0U, 1U, 1U);
    for(uint32_t i = 0U; i < (uint32_t)frame.width * frame.height; i++)
        pixels[i] = 0xf800;
    assert(hk_color_find_largest(&tracker, &frame, NULL, &red, 1U, &blob));
    expect_blob(blob, (uint32_t)frame.width * frame.height, 0U, 0U, frame.width, frame.height);
}

int main(void)
{
    test_bridge_and_rotation();
    test_maximum_frame();
    return 0;
}
