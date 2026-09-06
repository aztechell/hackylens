#include <assert.h>
#include <stdio.h>
#include "../firmware/src/services/resource_cleanup.h"
static unsigned calls, other_calls;
static uint8_t cleanup(void) { return ++calls == 2; }
static uint8_t other(void) { other_calls++; return 1; }
int main(void)
{
 resource_cleanup_poll(); assert(!calls);
 assert(resource_cleanup_schedule(RESOURCE_CLEANUP_KPU,cleanup));
 assert(!resource_cleanup_schedule(RESOURCE_CLEANUP_KPU,other));
 assert(resource_cleanup_schedule(RESOURCE_CLEANUP_CORE1,other));
 resource_cleanup_poll(); assert(calls==1 && other_calls==1);
 resource_cleanup_poll(); assert(calls==2 && other_calls==1);
 resource_cleanup_poll(); assert(calls==2 && other_calls==1);
 assert(resource_cleanup_schedule(RESOURCE_CLEANUP_KPU,other));
 resource_cleanup_poll(); assert(other_calls==2);
 assert(!resource_cleanup_schedule(RESOURCE_CLEANUP_COUNT,other));
 puts("S7_CLEANUP_OK pending_only=1 collision=1 released=1");
}
