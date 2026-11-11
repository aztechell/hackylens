#ifndef HK_FACE_RECOGNITION_IMAGE_H
#define HK_FACE_RECOGNITION_IMAGE_H

#include "../../services/face_detect_detector.h"
#include "face_recognition_models.h"

void face_recognition_crop(const uint8_t *frame, const face_detect_box_t *box, uint8_t *output);
uint8_t face_recognition_align(const uint8_t *frame, const face_detect_box_t *box,
                              const float *landmarks, uint8_t *output);
uint8_t face_recognition_encode(const float *embedding, int8_t *feature);
float face_recognition_compare(const int8_t *left, const int8_t *right);

#endif
