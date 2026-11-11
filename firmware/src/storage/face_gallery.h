#ifndef HK_FACE_GALLERY_H
#define HK_FACE_GALLERY_H

#include <stdint.h>

#define FACE_GALLERY_DIRECTORY "/hackylens.kmodels/faces"
#define FACE_GALLERY_DIM 192U
#define FACE_GALLERY_CAPACITY 10U

typedef enum
{
    FACE_GALLERY_OK = 0,
    FACE_GALLERY_IO,
    FACE_GALLERY_OCCUPIED,
    FACE_GALLERY_FULL,
    FACE_GALLERY_CORRUPT,
} face_gallery_result_t;

typedef struct
{
    uint16_t occupied;
    uint16_t invalid;
    int8_t features[FACE_GALLERY_CAPACITY][FACE_GALLERY_DIM];
} face_gallery_t;

face_gallery_result_t face_gallery_load(face_gallery_t *gallery, uint32_t model_crc);
face_gallery_result_t face_gallery_store(face_gallery_t *gallery, uint8_t id,
                                         uint32_t model_crc, const int8_t *feature);
face_gallery_result_t face_gallery_forget(face_gallery_t *gallery, uint8_t id);

#endif
