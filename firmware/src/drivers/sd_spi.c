#include "sd_spi.h"

#include "../board/board_pins.h"

#include "../board/board_hackylens.h"
#include "../hal/hal_gpio.h"
#include "../hal/hal_spi.h"

void sd_spi_cs_high(void)
{
    hal_gpiohs_write(GPIOHS_SD_CS, 1);
}

void sd_spi_cs_low(void)
{
    hal_gpiohs_write(GPIOHS_SD_CS, 0);
}

void sd_spi_config(uint32_t hz)
{
    hal_spi_fifo_config(SD_SPI, hz, SD_SPI_CS);
}

uint8_t sd_spi_xfer(uint8_t out)
{
    return hal_spi_fifo_xfer_u8(SD_SPI, out);
}

void sd_spi_board_init(uint32_t init_hz)
{
    board_sd_spi_init_pins();
    sd_spi_cs_high();
    sd_spi_config(init_hz);
}
