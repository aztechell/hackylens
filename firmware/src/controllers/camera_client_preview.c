#include "camera_client_preview.h"

#include "../services/camera_client.h"
#include "../ui/camera_view.h"

hk_result_t camera_client_present(const void *owner, uint32_t lease,
                                  const hk_color_rect_t *box, uint16_t color)
{
    camera_preview_frame_t frame;
    camera_view_present_t present;
    hk_result_t result = camera_client_get(owner, lease, &frame);
    if(result != HK_OK)
        return result;
    const uint16_t width = frame.rotate ? frame.height : frame.width;
    const uint16_t height = frame.rotate ? frame.width : frame.height;
    if(box && (!box->width || !box->height ||
               (uint32_t)box->x + box->width > width ||
               (uint32_t)box->y + box->height > height))
        return HK_ERR_INVALID_ARGUMENT;

    const camera_view_frame_t image = {
        .pixels = frame.pixels,
        .width = frame.width,
        .height = frame.height,
        .rotate = frame.rotate,
        .fit = IMAGE_VIEWPORT_CONTAIN,
    };
    if(!camera_view_compose_frame(&image, &present))
        return HK_ERR_BUSY;
    if(box)
    {
        /* The public box uses logical coordinates; Camera View maps sensor coordinates. */
        const camera_view_rect_t sensor = frame.rotate ?
            (camera_view_rect_t){box->y, frame.height - box->x - box->width,
                                 box->height, box->width} :
            (camera_view_rect_t){box->x, box->y, box->width, box->height};
        camera_view_compose_rects(&present, &image, &sensor, 1U, color);
    }
    return camera_view_present(&present) ? HK_OK : HK_ERR_IO;
}
