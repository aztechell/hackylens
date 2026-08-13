#include "time_internal.h"

#include "../../../platforms/k210/hal/hal_time.h"

uint64_t time_internal_us(void)
{
    return hal_time_us();
}

void time_internal_sleep_ms(uint32_t milliseconds)
{
    hal_sleep_ms(milliseconds);
}
