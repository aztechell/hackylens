#ifndef HK_TIME_INTERNAL_H
#define HK_TIME_INTERNAL_H

#include <stdint.h>

uint64_t time_internal_us(void);
void time_internal_sleep_ms(uint32_t milliseconds);

#endif
