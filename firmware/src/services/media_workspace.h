#ifndef HK_MEDIA_WORKSPACE_H
#define HK_MEDIA_WORKSPACE_H

#include <stdint.h>

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

/* Exclusive core-0 workspace. The owner remains live until release. */
media_workspace_t *media_workspace_claim(const void *owner);
void media_workspace_release(const void *owner);

#endif
