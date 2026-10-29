#include <assert.h>
#include <stdio.h>

#include "../firmware/src/core/image_viewport.h"
#include "../firmware/src/ui/hk_ui.h"

int main(void)
{
    image_viewport_t view;
    assert(image_viewport_calculate(240, 320, 240, 240, IMAGE_VIEWPORT_CONTAIN, &view));
    assert(view.src_x == 0 && view.src_y == 0 && view.src_w == 240 && view.src_h == 320);
    assert(view.dst_x == 30 && view.dst_y == 0 && view.dst_w == 180 && view.dst_h == 240);
    assert(image_viewport_calculate(240, 320, 240, 240, IMAGE_VIEWPORT_COVER, &view));
    assert(view.src_x == 0 && view.src_y == 40 && view.src_w == 240 && view.src_h == 240);
    assert(view.dst_x == 0 && view.dst_y == 0 && view.dst_w == 240 && view.dst_h == 240);
    assert(image_viewport_calculate(320, 240, 240, 240, IMAGE_VIEWPORT_COVER, &view));
    assert(view.src_x == 40 && view.src_y == 0 && view.src_w == 240 && view.src_h == 240);
    assert(image_viewport_calculate(320, 240, 320, 240, IMAGE_VIEWPORT_COVER, &view));
    assert(view.src_x == 0 && view.src_y == 0 && view.src_w == 320 && view.src_h == 240);
    assert(view.dst_x == 0 && view.dst_y == 0 && view.dst_w == 320 && view.dst_h == 240);

    /* Every mode stays within the source and destination, including odd and
     * extreme aspect ratios; the fitted dimensions differ only by rounding. */
    const uint16_t sizes[][4] = {
        {321, 241, 239, 237}, {1, 65535, 65535, 1},
        {65535, 1, 1, 65535}, {65535, 65535, 65535, 65535},
        {160, 120, 240, 240}, {480, 640, 240, 240},
    };
    for(unsigned index = 0; index < sizeof(sizes) / sizeof(sizes[0]); index++)
        for(unsigned mode = IMAGE_VIEWPORT_CONTAIN; mode <= IMAGE_VIEWPORT_COVER; mode++)
        {
            const uint16_t *size = sizes[index];
            assert(image_viewport_calculate(size[0], size[1], size[2], size[3], mode, &view));
            assert(view.src_w && view.src_h && view.dst_w && view.dst_h);
            assert((uint32_t)view.src_x + view.src_w <= size[0]);
            assert((uint32_t)view.src_y + view.src_h <= size[1]);
            assert((uint32_t)view.dst_x + view.dst_w <= size[2]);
            assert((uint32_t)view.dst_y + view.dst_h <= size[3]);
            if(mode == IMAGE_VIEWPORT_COVER)
                assert(view.dst_w == size[2] && view.dst_h == size[3]);
            else
                assert(view.src_w == size[0] && view.src_h == size[1]);
        }
    assert(!image_viewport_calculate(0, 320, 240, 240, IMAGE_VIEWPORT_COVER, &view));
    assert(!view.src_w && !view.dst_w);
    assert(!image_viewport_calculate(240, 320, 0, 240, IMAGE_VIEWPORT_COVER, &view));
    assert(!image_viewport_calculate(240, 320, 240, 240, 99, &view));
    assert(!image_viewport_calculate(240, 320, 240, 240, IMAGE_VIEWPORT_COVER, NULL));

    /* The existing file-viewer API continues to show the entire image. */
    uint16_t x, y, w, h;
    image_fit_viewport(320, 240, &x, &y, &w, &h);
    assert(x == 0 && y == 30 && w == 240 && h == 180);
    image_fit_viewport(0, 0, &x, &y, &w, &h);
    assert(x == 0 && y == 0 && w == 240 && h == 240);
    puts("IMAGE_VIEWPORT_OK contain/cover bounds=checked files=contain");
    return 0;
}
