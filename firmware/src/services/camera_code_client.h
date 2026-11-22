#ifndef HK_CAMERA_CODE_CLIENT_H
#define HK_CAMERA_CODE_CLIENT_H

#include <hackylens/capability/common.h>
#include <hackylens/capability/vision.h>

hk_result_t camera_code_client_begin(const void *owner, uint32_t lease, vision_source_t source);
hk_result_t camera_code_client_poll(const void *owner, vision_result_snapshot_t *snapshot);
uint8_t camera_code_client_holds(const void *owner, uint32_t lease);
void camera_code_client_close(const void *owner);

#endif
