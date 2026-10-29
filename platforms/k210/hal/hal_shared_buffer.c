#include "hal_shared_buffer.h"

#include <stdint.h>

void *hal_shared_buffer_uncached(void *pointer)
{
    uintptr_t address = (uintptr_t)pointer;

    if(address >= 0x80000000UL && address < 0x80600000UL)
        address -= 0x40000000UL;
    return (void *)address;
}
