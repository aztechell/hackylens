#ifndef HK_CAMERA_LAYOUT_H
#define HK_CAMERA_LAYOUT_H

#include "../core/image_viewport.h"

/* Live previews fill the display while preserving the scene's proportions. */
#define CAMERA_PREVIEW_FIT IMAGE_VIEWPORT_COVER

#define CAMERA_OK_HOLD_US 700000ULL
#define CAMERA_LIGHT_REPEAT_INITIAL_US 500000ULL
#define CAMERA_LIGHT_REPEAT_NEXT_US 180000ULL

#endif
