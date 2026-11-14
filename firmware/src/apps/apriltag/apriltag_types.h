#ifndef HK_APRILTAG_TYPES_H
#define HK_APRILTAG_TYPES_H

#include <stdint.h>

#include "../../services/apriltag_decoder.h"

typedef enum
{
    APRILTAG_OUTPUT_ALL = 0,
    APRILTAG_OUTPUT_SELECTED,
} apriltag_output_mode_t;

typedef struct
{
    uint8_t refine_edges;
    apriltag_output_mode_t output_mode;
    uint8_t fps_enabled;
    uint8_t far_search;
    uint8_t light_mode;
    uint8_t rgb_red;
    uint8_t rgb_green;
    uint8_t rgb_blue;
} apriltag_preferences_t;

#endif
