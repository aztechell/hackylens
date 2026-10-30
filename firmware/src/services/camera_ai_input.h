#ifndef HK_CAMERA_AI_INPUT_H
#define HK_CAMERA_AI_INPUT_H

#include <stdint.h>

/*
 * One camera/KPU application is active at a time. This shared planar RGB888
 * slot avoids permanently reserving a separate 230 KiB DVP input in every
 * feature module.
 */
uint8_t *camera_ai_input_acquire(const void *owner,
                                 uint16_t width,
                                 uint16_t height,
                                 uint32_t input_bytes);
uint8_t *camera_ai_input_data(const void *owner);
uint8_t camera_ai_input_attach(const void *owner);
uint8_t camera_ai_input_take(const void *owner, uint32_t *sequence);
void camera_ai_input_prepare_preview(const volatile uint16_t *pixels,
                                     uint16_t width, uint16_t height,
                                     uint32_t sequence);
uint8_t camera_ai_input_map_rect(const void *owner, int16_t *x, int16_t *y,
                                 int16_t *w, int16_t *h);
void camera_ai_input_arm(const void *owner);
void camera_ai_input_cancel(const void *owner);
void camera_ai_input_release(const void *owner);

#endif
