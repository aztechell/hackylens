#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../firmware/src/core/shared_buffer.h"

#define CHECK(condition) do { if(!(condition)) { \
    fprintf(stderr, "FAIL line=%d: %s\n", __LINE__, #condition); \
    exit(1); } } while(0)

static uintptr_t mapped(uintptr_t address)
{
    return (uintptr_t)hk_shared_buffer_uncached((void *)address);
}

int main(void)
{
    CHECK(mapped(0U) == 0U);
    CHECK(mapped(0x7FFFFFFFUL) == 0x7FFFFFFFUL);
#if defined(SHARED_BUFFER_K210_TEST)
    CHECK(mapped(0x80000000UL) == 0x40000000UL);
    CHECK(mapped(0x805FFFFFUL) == 0x405FFFFFUL);
#else
    CHECK(mapped(0x80000000UL) == 0x80000000UL);
    CHECK(mapped(0x805FFFFFUL) == 0x805FFFFFUL);
#endif
    CHECK(mapped(0x80600000UL) == 0x80600000UL);
    CHECK(mapped(0x90000000UL) == 0x90000000UL);
    puts("SHARED_BUFFER_OK");
    return 0;
}
