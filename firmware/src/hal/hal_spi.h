#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stddef.h>
#include <stdint.h>

void hal_spi_init(uint8_t device, uint32_t frame_bits);
uint32_t hal_spi_set_clock(uint8_t device, uint32_t hz);
void hal_spi0_enable_dvp_data(void);
void hal_spi_standard_init(uint8_t device, uint32_t hz);
void hal_spi_standard_send(uint8_t device, uint8_t chip_select, const uint8_t *cmd, size_t cmd_len, const uint8_t *data, size_t data_len);
void hal_spi_standard_receive(uint8_t device, uint8_t chip_select, const uint8_t *cmd, size_t cmd_len, uint8_t *data, size_t data_len);
void hal_spi_fifo_config(uint8_t device, uint32_t hz, uint8_t chip_select);
void hal_spi_fifo_set_tmod_tx(uint8_t device);
void hal_spi_fifo_set_frame_bits(uint8_t device, uint32_t bits);
void hal_spi_fifo_send_bytes(uint8_t device, uint8_t chip_select, const uint8_t *data, size_t len);
uint8_t hal_spi_fifo_xfer_u8(uint8_t device, uint8_t out);

#endif
