#ifndef HK_CAMERA_AI_TRANSFORM_H
#define HK_CAMERA_AI_TRANSFORM_H
#include <stdint.h>

/* Model input is upright planar RGB888, with centered black margins. */
uint8_t camera_ai_transform_rgb565(const volatile uint16_t *source,
                                  uint16_t width, uint16_t height,
                                  uint8_t rotate, uint8_t *planar);
uint8_t camera_ai_transform_rect(uint16_t width, uint16_t height,
                                uint8_t rotate, int16_t *x, int16_t *y,
                                int16_t *w, int16_t *h);
#endif
