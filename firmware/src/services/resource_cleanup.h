#ifndef HK_RESOURCE_CLEANUP_H
#define HK_RESOURCE_CLEANUP_H
#include <stdint.h>
typedef enum { RESOURCE_CLEANUP_KPU, RESOURCE_CLEANUP_CORE1, RESOURCE_CLEANUP_COUNT } resource_cleanup_slot_t;
/* Called on core0. Return one only when the resource is released or quarantined.
 * Callbacks own persistent resource state, never an app context or app state. */
typedef uint8_t (*resource_cleanup_fn)(void);
uint8_t resource_cleanup_schedule(resource_cleanup_slot_t slot, resource_cleanup_fn poll);
void resource_cleanup_poll(void);
#endif
