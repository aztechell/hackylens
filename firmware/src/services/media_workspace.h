#ifndef HK_MEDIA_WORKSPACE_H
#define HK_MEDIA_WORKSPACE_H

#include <stdint.h>

#include "../core/ai_model_types.h"

#define MEDIA_PNG_COMP_BYTES (512U * 1024U)
#define MEDIA_PNG_TABLE_ENTRIES (1U << 15U)
#define MEDIA_WORKSPACE_BYTES (MEDIA_PNG_COMP_BYTES + 4U * MEDIA_PNG_TABLE_ENTRIES)

typedef union
{
    uint8_t bytes[MEDIA_WORKSPACE_BYTES];
    struct
    {
        uint8_t compressed[MEDIA_PNG_COMP_BYTES];
        uint16_t literals[MEDIA_PNG_TABLE_ENTRIES];
        uint16_t distances[MEDIA_PNG_TABLE_ENTRIES];
    } png;
} media_workspace_t;

/* The model layout leaves two QVGA RGB565 slots for live camera capture. */
#define MEDIA_MODEL_CAMERA_BYTES (2U * 320U * 240U * 2U)
#define MEDIA_FACE_DETECTOR_RESERVE (400U * 1024U)
#define MEDIA_FACE_EMBEDDING_RESERVE 1108480U /* 1108368-byte model rounded up for DMA */
#define MEDIA_MODEL_WORKSPACE_BYTES (MEDIA_FACE_DETECTOR_RESERVE + MEDIA_FACE_EMBEDDING_RESERVE)

/* Exclusive core-0 leases on the frame arena tail. PNG can coexist with
 * the frame scratch borrow. Model weights can coexist with QVGA capture.
 * Keep the lease until all CPU and DMA access to the tail has ended. */
media_workspace_t *media_workspace_claim(const void *owner);
ai_model_workspace_t media_model_workspace_claim(const void *owner);
void media_workspace_release(const void *owner);

#endif
