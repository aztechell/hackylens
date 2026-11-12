#ifndef HK_VISION_RESULT_SERVICE_H
#define HK_VISION_RESULT_SERVICE_H

#include <hackylens/capability/vision.h>

/* Convert a half-open sensor rectangle to logical camera coordinates. */
vision_result_item_t vision_result_block(uint16_t id, uint8_t flags, uint16_t confidence,
                                         uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                                         uint16_t sensor_height, uint8_t rotate);
void vision_result_publish(vision_source_t source, uint16_t width, uint16_t height,
                           const vision_result_item_t *items, uint8_t count);
void vision_result_publish_qr(uint16_t width, uint16_t height,
                              const vision_result_item_t *block,
                              const uint8_t *payload, uint16_t length);
void vision_result_clear(vision_source_t source);

#endif
