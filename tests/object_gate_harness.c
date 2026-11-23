#include "object_gate_core.h"

#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    object_gate_state_t gate = {0};
    uint32_t digest = 0;
    unsigned width, height, count;
    while(scanf("%u %u %u", &width, &height, &count) == 3)
    {
        if(count > VISION_RESULT_MAX_ITEMS)
            return 1;
        vision_result_snapshot_t snapshot = {.width = width, .height = height, .count = count};
        for(unsigned i = 0; i < count; ++i)
        {
            unsigned kind, flags, id, x0, y0, x1, y1, confidence;
            if(scanf("%u %u %u %u %u %u %u %u", &kind, &flags, &id,
                     &x0, &y0, &x1, &y1, &confidence) != 8)
                return 1;
            snapshot.items[i] = (vision_result_item_t){kind, flags, id, x0, y0, x1, y1, confidence, 0};
        }
        object_gate_view_t view = object_gate_step(&gate, &snapshot);
        digest = object_gate_digest(digest, &snapshot);
        printf("%u %u %u %u %u %u %" PRIu32 " %" PRIu32 "\n",
               view.box.x, view.box.y, view.box.width, view.box.height, view.color,
               gate.occupied, gate.entries, digest);
    }
    return 0;
}
