#include "frame_pool.h"
#include "frame_workspace.h"
#include "media_workspace.h"

#include <stddef.h>

#include "../config/camera_config.h"

#if __has_include("hk_config.h")
#include "hk_config.h"
#else
/* The standalone host fixtures exercise all layouts. */
#define HK_ENABLE_APP_FILES 1
#define HK_ENABLE_APP_FACE_RECOGNITION 1
#define HK_ENABLE_APP_OBJECT_DETECT 1
#endif

#define FRAME_BYTES (CAMERA_MAX_FRAME_PIXELS * 2U)
#define CAMERA_BYTES (FRAME_POOL_CAMERA_SLOT_COUNT * FRAME_BYTES)
#define ALIGN_BYTES(n) (((n) + FRAME_POOL_ALIGNMENT - 1U) & ~(FRAME_POOL_ALIGNMENT - 1U))
#define PNG_LAYOUT_BYTES (HK_ENABLE_APP_FILES * (FRAME_BYTES + sizeof(media_workspace_t)))
#define HK_MODEL_WORKSPACE_ENABLED (HK_ENABLE_APP_FACE_RECOGNITION || HK_ENABLE_APP_OBJECT_DETECT)
#define MODEL_LAYOUT_BYTES (HK_MODEL_WORKSPACE_ENABLED * (MEDIA_MODEL_CAMERA_BYTES + MEDIA_MODEL_WORKSPACE_BYTES))
#define MAX_BYTES(a, b) ((a) > (b) ? (a) : (b))

typedef union
{
    uint16_t camera[CAMERA_BYTES / 2U];
    uint8_t bytes[MAX_BYTES(CAMERA_BYTES, MAX_BYTES(PNG_LAYOUT_BYTES, MODEL_LAYOUT_BYTES))];
#if HK_ENABLE_APP_FILES
    struct
    {
        uint16_t scratch[FRAME_BYTES / 2U];
        media_workspace_t png;
    } images;
#endif
} frame_arena_t;

static _Alignas(FRAME_POOL_ALIGNMENT) frame_arena_t g_memory;
#if HK_ENABLE_APP_FILES
_Static_assert(offsetof(frame_arena_t, images.png) == FRAME_BYTES, "PNG workspace offset");
#endif
_Static_assert(FRAME_BYTES % FRAME_POOL_ALIGNMENT == 0U, "frame workspace alignment");
_Static_assert(MEDIA_MODEL_CAMERA_BYTES % FRAME_POOL_ALIGNMENT == 0U, "model workspace alignment");
static uint32_t g_workspace_generation;
static uint32_t g_workspace_active_generation;
static uint32_t g_camera_stride;
static uint32_t g_camera_frame_bytes;
static uint32_t g_prefix_bytes = sizeof(g_memory);
#if HK_ENABLE_APP_FILES || HK_MODEL_WORKSPACE_ENABLED
static const void *g_aux_owner;
#endif

uint8_t frame_pool_camera_reserve(uint32_t frame_bytes)
{
    if(!frame_bytes || frame_bytes > FRAME_BYTES || g_camera_stride ||
       g_workspace_active_generation != 0U)
        return 0U;
    const uint32_t stride = ALIGN_BYTES(frame_bytes);
    if(stride * FRAME_POOL_CAMERA_SLOT_COUNT > g_prefix_bytes)
        return 0U;
    g_camera_stride = stride;
    g_camera_frame_bytes = frame_bytes;
    return 1U;
}

void frame_pool_camera_release(void)
{
    g_camera_stride = 0U;
    g_camera_frame_bytes = 0U;
}

uint16_t *frame_pool_camera_slot(uint8_t index)
{
    if(!g_camera_stride || index >= FRAME_POOL_CAMERA_SLOT_COUNT)
        return NULL;
    return &g_memory.camera[index * g_camera_stride / 2U];
}

uint32_t frame_pool_camera_frame_bytes(void)
{
    return g_camera_frame_bytes;
}

#if HK_ENABLE_APP_FILES || HK_MODEL_WORKSPACE_ENABLED
static uint8_t claim_aux(const void *owner, uint32_t prefix_bytes)
{
    if(!owner || g_aux_owner ||
       g_camera_stride * FRAME_POOL_CAMERA_SLOT_COUNT > prefix_bytes ||
       (g_workspace_active_generation && FRAME_BYTES > prefix_bytes))
        return 0U;
    g_aux_owner = owner;
    g_prefix_bytes = prefix_bytes;
    return 1U;
}

void media_workspace_release(const void *owner)
{
    if(owner && owner == g_aux_owner)
    {
        g_aux_owner = NULL;
        g_prefix_bytes = sizeof(g_memory);
    }
}
#endif

#if HK_ENABLE_APP_FILES
media_workspace_t *media_workspace_claim(const void *owner)
{
    return claim_aux(owner, FRAME_BYTES) ? &g_memory.images.png : NULL;
}
#endif

#if HK_MODEL_WORKSPACE_ENABLED
ai_model_workspace_t media_model_workspace_claim(const void *owner)
{
    if(!claim_aux(owner, MEDIA_MODEL_CAMERA_BYTES))
        return (ai_model_workspace_t){0};
    return (ai_model_workspace_t){g_memory.bytes + MEDIA_MODEL_CAMERA_BYTES,
                                  MEDIA_MODEL_WORKSPACE_BYTES};
}
#endif

uint8_t frame_workspace_borrow(
    uint32_t minimum_size, frame_workspace_borrow_t *borrow)
{
    uint32_t generation;

    if(!borrow || minimum_size == 0U ||
       minimum_size > FRAME_BYTES || g_camera_stride || FRAME_BYTES > g_prefix_bytes ||
       g_workspace_active_generation != 0U ||
       g_workspace_generation == UINT32_MAX)
        return 0U;
    generation = g_workspace_generation + 1U;
    g_workspace_generation = generation;
    g_workspace_active_generation = generation;
    *borrow = (frame_workspace_borrow_t){
        g_memory.bytes, FRAME_BYTES, generation,
    };
    return 1U;
}

uint8_t frame_workspace_release(frame_workspace_borrow_t *borrow)
{
    uint8_t valid;

    if(!borrow)
        return 0U;
    valid = (uint8_t)(borrow->generation != 0U &&
                      borrow->generation == g_workspace_active_generation &&
                      borrow->data == g_memory.bytes);
    if(valid)
        g_workspace_active_generation = 0U;
    *borrow = FRAME_WORKSPACE_BORROW_NONE;
    return valid;
}

#if defined(HK_FRAME_POOL_TESTING)
uint8_t frame_workspace_test_set_generation(uint32_t generation)
{
    if(g_camera_stride || g_workspace_active_generation != 0U)
        return 0U;
    g_workspace_generation = generation;
    return 1U;
}
#endif
