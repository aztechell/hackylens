#ifndef HK_FACE_DETECT_MODEL_STORAGE_H
#define HK_FACE_DETECT_MODEL_STORAGE_H

#include <stdint.h>

#include "face_detect_types.h"

face_model_storage_result_t face_detect_model_storage_load(uint8_t **buffer, uint32_t *size);
void face_detect_model_storage_free(uint8_t *buffer);

#endif
