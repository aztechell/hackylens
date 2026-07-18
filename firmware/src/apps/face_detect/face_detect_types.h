#ifndef HK_FACE_DETECT_TYPES_H
#define HK_FACE_DETECT_TYPES_H

#include <stdint.h>

#define FACE_DETECT_BOX_MAX 8U

typedef enum
{
    FACE_DETECT_LOAD_OK = 0,
    FACE_DETECT_LOAD_NO_SD,
    FACE_DETECT_LOAD_DIR,
    FACE_DETECT_LOAD_FILE,
    FACE_DETECT_LOAD_READ,
    FACE_DETECT_LOAD_ALLOC,
    FACE_DETECT_LOAD_FORMAT,
    FACE_DETECT_LOAD_KPU,
} face_detect_load_result_t;

typedef enum
{
    FACE_MODEL_STORAGE_OK = 0,
    FACE_MODEL_STORAGE_NO_SD,
    FACE_MODEL_STORAGE_DIR,
    FACE_MODEL_STORAGE_FILE,
    FACE_MODEL_STORAGE_ALLOC,
    FACE_MODEL_STORAGE_READ,
} face_model_storage_result_t;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} face_detect_box_t;

#endif
