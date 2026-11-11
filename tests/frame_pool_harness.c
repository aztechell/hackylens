#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "frame_pool.h"
#include "frame_workspace.h"
#include "media_workspace.h"

static void require_true(unsigned condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

int main(void)
{
    frame_workspace_borrow_t first = FRAME_WORKSPACE_BORROW_NONE;
    frame_workspace_borrow_t copied;
    frame_workspace_borrow_t copied_final;
    frame_workspace_borrow_t second = FRAME_WORKSPACE_BORROW_NONE;
    frame_workspace_borrow_t blocked = FRAME_WORKSPACE_BORROW_NONE;
    const uint32_t vga = 640U * 480U * 2U;
    const uint32_t qvga = MEDIA_MODEL_CAMERA_BYTES / FRAME_POOL_CAMERA_SLOT_COUNT;
    const char models = 0, png = 0;

    require_true(frame_workspace_borrow(4096U, &first), "first scratch borrow");
    copied = first;
    copied_final = first;
    require_true(first.data != NULL && first.size >= 4096U,
                 "borrowed view has required capacity");
    require_true(!frame_workspace_borrow(1U, &blocked),
                 "second scratch borrower is rejected");
    require_true(!frame_pool_camera_reserve(vga),
                 "camera reservation conflicts with scratch");
    require_true(!media_model_workspace_claim(&models).data,
                 "models cannot overlap a live scratch borrow");
    media_workspace_t *media = media_workspace_claim(&png);
    require_true(media && (uint8_t *)media == first.data + first.size,
                 "PNG and decoded pixels fit in disjoint regions");
    media_workspace_release(&png);
    require_true(frame_workspace_release(&first), "matching release succeeds");
    require_true(first.data == NULL && first.generation == 0U,
                 "release invalidates caller token");

    require_true(frame_pool_camera_reserve(vga), "camera reserves both slots");
    require_true(frame_pool_camera_slot(0U) != NULL &&
                 frame_pool_camera_slot(1U) != NULL,
                 "reserved camera slots are available");
    require_true(frame_pool_camera_slot(FRAME_POOL_CAMERA_SLOT_COUNT) == NULL,
                 "camera slot bound is checked");
    require_true(!frame_workspace_borrow(1U, &blocked),
                 "scratch conflicts with camera reservation");
    require_true(!media_model_workspace_claim(&models).data && !media_workspace_claim(&png),
                 "VGA protects the full camera reservation");
    frame_pool_camera_release();

    const ai_model_workspace_t weights = media_model_workspace_claim(&models);
    require_true(weights.data && weights.bytes >= 1352588U &&
                     (uintptr_t)weights.data % FRAME_POOL_ALIGNMENT == 0U,
                 "stock object model fits aligned arena");
    media_workspace_release(&png);
    require_true(!media_workspace_claim(&png) && !frame_pool_camera_reserve(vga) &&
                     !frame_workspace_borrow(1U, &blocked),
                 "model lease blocks conflicting reservations and foreign release");
    require_true(frame_pool_camera_reserve(qvga), "QVGA fits with model storage");
    require_true(frame_pool_camera_frame_bytes() == qvga &&
                     (uint8_t *)frame_pool_camera_slot(1U) + qvga == weights.data,
                 "capture stride and model workspace are disjoint");
    frame_pool_camera_release();
    require_true(!frame_pool_camera_reserve(vga),
                 "stopping capture does not release weights while KPU unloads");
    media_workspace_release(&models);
    require_true(frame_pool_camera_reserve(vga), "VGA works again after model release");
    frame_pool_camera_release();

    require_true(frame_workspace_borrow(8192U, &second),
                 "scratch can be borrowed after camera release");
    require_true(second.generation != copied.generation,
                 "borrow generation advances");
    require_true(!frame_workspace_release(&copied),
                 "stale copied token cannot release later borrow");
    require_true(!frame_workspace_borrow(1U, &blocked),
                 "stale release leaves current borrower active");
    require_true(frame_workspace_release(&second),
                 "current borrower releases normally");
    require_true(!frame_workspace_borrow(
                     vga + 1U, &blocked),
                 "oversized borrow is rejected");

    require_true(frame_workspace_test_set_generation(UINT32_MAX - 2U),
                 "fixture positions generation near exhaustion");
    require_true(frame_workspace_borrow(1U, &second),
                 "penultimate generation is issued");
    require_true(second.generation == UINT32_MAX - 1U,
                 "generation increments exactly once near exhaustion");
    require_true(!frame_workspace_release(&copied),
                 "old generation-one token cannot release penultimate borrow");
    require_true(frame_workspace_release(&second),
                 "penultimate borrow releases normally");
    require_true(frame_workspace_borrow(1U, &second),
                 "final generation is issued");
    require_true(second.generation == UINT32_MAX,
                 "final generation never wraps");
    require_true(!frame_workspace_release(&copied_final),
                 "old generation-one token cannot release final borrow");
    require_true(frame_workspace_release(&second),
                 "final borrow releases normally");
    require_true(!frame_workspace_borrow(1U, &blocked),
                 "generation exhaustion permanently blocks future borrows");
    require_true(blocked.generation == 0U,
                 "exhaustion does not issue generation one");

    puts("FRAME_POOL_OK borrow=exclusive stale=blocked camera=exclusive "
         "generation=exhausted");
    return 0;
}
