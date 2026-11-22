#ifndef HK_CAMERA_CLIENT_PREVIEW_H
#define HK_CAMERA_CLIENT_PREVIEW_H

#include <hackylens/capability/camera_types.h>
#include <hackylens/capability/common.h>

hk_result_t camera_client_present(const void *owner, uint32_t lease,
                                  const hk_color_rect_t *box, uint16_t color);

#endif
