#ifndef HK_FACE_MODEL_STORAGE_H
#define HK_FACE_MODEL_STORAGE_H

#include <stdint.h>

typedef enum
{
    FACE_MODEL_STORAGE_OK = 0,
    FACE_MODEL_STORAGE_NO_SD,
    FACE_MODEL_STORAGE_DIR,
    FACE_MODEL_STORAGE_FILE,
    FACE_MODEL_STORAGE_ALLOC,
    FACE_MODEL_STORAGE_READ,
} face_model_storage_result_t;

face_model_storage_result_t face_model_storage_load(uint8_t **buffer, uint32_t *size);
void face_model_storage_free(uint8_t *buffer);

#endif
