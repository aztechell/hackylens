#ifndef HK_APRILTAG_DISTANCE_H
#define HK_APRILTAG_DISTANCE_H

#include <stdint.h>

#define APRILTAG_DISTANCE_TAG_DEFAULT_MM 60U
#define APRILTAG_DISTANCE_REFERENCE_DEFAULT_MM 300U
#define APRILTAG_DISTANCE_FOCAL_MIN_MPX 30000U
#define APRILTAG_DISTANCE_FOCAL_MAX_MPX 400000U
#define APRILTAG_DISTANCE_FOCAL_MIN_PX (APRILTAG_DISTANCE_FOCAL_MIN_MPX / 1000.0f)
#define APRILTAG_DISTANCE_FOCAL_MAX_PX (APRILTAG_DISTANCE_FOCAL_MAX_MPX / 1000.0f)

typedef struct
{
    float x;
    float y;
    float z;
} apriltag_vector_t;

/* Coordinates are in the 160x120 detector image. The prototype assumes square
 * pixels and a centered principal point. All translations are in millimeters. */
uint8_t apriltag_distance_calibrate(const float corners[4][2], uint16_t tag_mm,
                                   uint16_t reference_mm, float *focal_px);
uint8_t apriltag_distance_pose(const float corners[4][2], uint16_t tag_mm,
                              float focal_px, uint8_t native_resolution,
                              apriltag_vector_t *translation);
float apriltag_distance_norm(apriltag_vector_t v);
uint8_t apriltag_distance_horizontal(apriltag_vector_t translation,
                                     const int16_t upright[3], const int16_t lens_down[3],
                                     const int16_t current[3], float *horizontal_mm);

#endif
