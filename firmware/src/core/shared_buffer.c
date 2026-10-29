#include "shared_buffer.h"

#include "hal_shared_buffer.h"

void *hk_shared_buffer_uncached(void *pointer)
{
    return hal_shared_buffer_uncached(pointer);
}
