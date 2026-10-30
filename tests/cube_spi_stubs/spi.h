#ifndef CUBE_TEST_SPI_H
#define CUBE_TEST_SPI_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint32_t ctrlr0, ctrlr1, ssienr, ser, baudr, txflr, rxflr, sr;
    uint32_t dr[36];
    uint32_t spi_ctrlr0, dmacr;
} spi_t;
typedef unsigned spi_device_num_t;
typedef unsigned spi_chip_select_t;
#define SPI_WORK_MODE_0 0U
#define SPI_FF_STANDARD 0U
#define SPI_FF_OCTAL 3U
#define SPI_AITM_AS_FRAME_FORMAT 2U
#define SPI_TMOD_TRANS 1U
#define SPI_TMOD_RECV 2U
#define SPI_TMOD_EEROM 3U
extern spi_t cube_spi_registers[4];
void spi_init(unsigned device, unsigned mode, unsigned format, unsigned bits, unsigned endian);
void spi_init_non_standard(unsigned device, unsigned instruction, unsigned address,
                           unsigned wait, unsigned mode);
uint32_t spi_set_clk_rate(unsigned device, uint32_t rate);
void spi_send_data_standard(unsigned device, unsigned chip,
    const uint8_t *cmd, size_t cmd_size, const uint8_t *data, size_t data_size);
void spi_receive_data_standard(unsigned device, unsigned chip,
    const uint8_t *cmd, size_t cmd_size, uint8_t *data, size_t data_size);
#endif
