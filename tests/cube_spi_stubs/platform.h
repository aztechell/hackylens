#ifndef CUBE_TEST_PLATFORM_H
#define CUBE_TEST_PLATFORM_H
#include "spi.h"
#define SPI0_BASE_ADDR ((uintptr_t)&cube_spi_registers[0])
#define SPI1_BASE_ADDR ((uintptr_t)&cube_spi_registers[1])
#define SPI3_BASE_ADDR ((uintptr_t)&cube_spi_registers[3])
static inline uint64_t read_cycle(void) { return 1U; }
#endif
