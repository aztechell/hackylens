#ifndef SD_SPI_H
#define SD_SPI_H

#include <stddef.h>
#include <stdint.h>

void sd_spi_board_init(uint32_t init_hz);
void sd_spi_config(uint32_t hz);
void sd_spi_cs_high(void);
void sd_spi_cs_low(void);
uint8_t sd_spi_xfer(uint8_t out);
uint8_t sd_spi_read_bytes(uint8_t *data, size_t size, uint64_t deadline_us);

#endif
