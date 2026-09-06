#include "resource_cleanup.h"
#include <stddef.h>
static resource_cleanup_fn s_pending[RESOURCE_CLEANUP_COUNT];
uint8_t resource_cleanup_schedule(resource_cleanup_slot_t slot, resource_cleanup_fn poll)
{
    if((unsigned)slot >= RESOURCE_CLEANUP_COUNT || !poll ||
       (s_pending[slot] && s_pending[slot] != poll))
        return 0U;
    s_pending[slot] = poll;
    return 1U;
}
void resource_cleanup_poll(void)
{
    for(unsigned slot = 0U; slot < RESOURCE_CLEANUP_COUNT; slot++)
    {
        resource_cleanup_fn poll = s_pending[slot];
        if(poll && poll())
            s_pending[slot] = NULL;
    }
}
