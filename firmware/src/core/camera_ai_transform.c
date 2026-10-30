#include "camera_ai_transform.h"
#include "image_viewport.h"
#include <string.h>

static uint8_t viewport(uint16_t width, uint16_t height, uint8_t rotate,
                        image_viewport_t *view)
{
    return width <= 320U && height <= 240U && rotate <= 1U &&
        image_viewport_calculate(rotate ? height : width, rotate ? width : height,
                                 width, height, IMAGE_VIEWPORT_CONTAIN, view);
}

uint8_t camera_ai_transform_rgb565(const volatile uint16_t *source,
                                  uint16_t width, uint16_t height,
                                  uint8_t rotate, uint8_t *planar)
{
    image_viewport_t view;
    uint16_t columns[320], rows[240];
    uint32_t plane = (uint32_t)width * height;
    if(!source || !planar || !viewport(width, height, rotate, &view)) return 0;
    memset(planar, 0, plane * 3U);
    for(uint16_t x = 0; x < view.dst_w; x++)
        columns[x] = (uint32_t)x * view.src_w / view.dst_w;
    for(uint16_t y = 0; y < view.dst_h; y++)
        rows[y] = (uint32_t)y * view.src_h / view.dst_h;
    for(uint16_t y = 0; y < view.dst_h; y++)
        for(uint16_t x = 0; x < view.dst_w; x++)
        {
            uint32_t source_index = rotate ?
                (uint32_t)(height - 1U - columns[x]) * width + rows[y] :
                (uint32_t)rows[y] * width + columns[x];
            uint16_t pixel = source[source_index];
            uint32_t target = (uint32_t)(y + view.dst_y) * width + x + view.dst_x;
            uint8_t r = (pixel >> 11) & 31U;
            uint8_t g = (pixel >> 5) & 63U;
            uint8_t b = pixel & 31U;
            planar[target] = (r << 3) | (r >> 2);
            planar[plane + target] = (g << 2) | (g >> 4);
            planar[plane * 2U + target] = (b << 3) | (b >> 2);
        }
    return 1;
}

uint8_t camera_ai_transform_rect(uint16_t width, uint16_t height,
                                uint8_t rotate, int16_t *x, int16_t *y,
                                int16_t *w, int16_t *h)
{
    image_viewport_t view;
    if(!x || !y || !w || !h || *w <= 0 || *h <= 0 ||
       !viewport(width, height, rotate, &view)) return 0;
    int32_t x0 = *x > view.dst_x ? *x : view.dst_x;
    int32_t y0 = *y > view.dst_y ? *y : view.dst_y;
    int32_t x1 = (int32_t)*x + *w;
    int32_t y1 = (int32_t)*y + *h;
    if(x1 > view.dst_x + view.dst_w) x1 = view.dst_x + view.dst_w;
    if(y1 > view.dst_y + view.dst_h) y1 = view.dst_y + view.dst_h;
    if(x1 <= x0 || y1 <= y0) return 0;
    uint16_t u0 = (uint32_t)(x0 - view.dst_x) * view.src_w / view.dst_w;
    uint16_t v0 = (uint32_t)(y0 - view.dst_y) * view.src_h / view.dst_h;
    uint16_t u1 = ((uint32_t)(x1 - view.dst_x) * view.src_w + view.dst_w - 1U) / view.dst_w;
    uint16_t v1 = ((uint32_t)(y1 - view.dst_y) * view.src_h + view.dst_h - 1U) / view.dst_h;
    *x = rotate ? v0 : u0;
    *y = rotate ? height - u1 : v0;
    *w = rotate ? v1 - v0 : u1 - u0;
    *h = rotate ? u1 - u0 : v1 - v0;
    return *w > 0 && *h > 0;
}
