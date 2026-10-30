#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "camera_ai_transform.h"

static uint16_t source[320 * 240];
static uint8_t input[320 * 240 * 3 + 16];
static void color(unsigned x, unsigned y, unsigned r, unsigned g, unsigned b)
{
    unsigned i = y * 320 + x;
    assert(input[i] == r && input[76800 + i] == g && input[153600 + i] == b);
}
int main(void)
{
    for(unsigned y = 0; y < 240; y++)
        for(unsigned x = 0; x < 320; x++)
            source[y * 320 + x] = y < 120 ? (x < 160 ? 0xF800 : 0x07E0) :
                                                       (x < 160 ? 0x001F : 0xFFFF);
    memset(input, 0xA5, sizeof(input));
    assert(camera_ai_transform_rgb565(source, 320, 240, 0, input));
    color(10, 10, 255, 0, 0); color(310, 10, 0, 255, 0);
    color(10, 230, 0, 0, 255); color(310, 230, 255, 255, 255);
    assert(camera_ai_transform_rgb565(source, 320, 240, 1, input));
    color(80, 10, 0, 0, 255); color(240, 10, 255, 0, 0);
    color(80, 230, 255, 255, 255); color(240, 230, 0, 255, 0);
    for(unsigned y = 0; y < 240; y++)
    {
        color(0, y, 0, 0, 0); color(69, y, 0, 0, 0);
        color(250, y, 0, 0, 0); color(319, y, 0, 0, 0);
    }
    for(unsigned i = 230400; i < sizeof(input); i++) assert(input[i] == 0xA5);
    int16_t x = 70, y = 0, w = 180, h = 240;
    assert(camera_ai_transform_rect(320, 240, 1, &x, &y, &w, &h));
    assert(x == 0 && y == 0 && w == 320 && h == 240);
    x = 70; y = 0; w = 90; h = 120;
    assert(camera_ai_transform_rect(320, 240, 1, &x, &y, &w, &h));
    assert(x == 0 && y == 120 && w == 160 && h == 120);
    x = 0; y = 0; w = 50; h = 240;
    assert(!camera_ai_transform_rect(320, 240, 1, &x, &y, &w, &h));
    x = 20; y = 30; w = 40; h = 50;
    assert(camera_ai_transform_rect(320, 240, 0, &x, &y, &w, &h));
    assert(x == 20 && y == 30 && w == 40 && h == 50);
    assert(!camera_ai_transform_rgb565(source, 0, 240, 1, input));
    assert(!camera_ai_transform_rgb565(source, 321, 240, 1, input));
    assert(!camera_ai_transform_rgb565(source, 320, 240, 2, input));
    puts("CAMERA_AI_TRANSFORM_OK");
    return 0;
}
