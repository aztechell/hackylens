#include "boot_flash.h"

#include <stddef.h>

#include "../board/board_pins.h"

#include "../hal/hal_spi.h"

static void boot_flash_cmd(const uint8_t *cmd, size_t cmd_len)
{
    hal_spi_standard_send(FLASH_SPI, FLASH_SPI_CS, cmd, cmd_len, NULL, 0);
}

void boot_flash_init(uint32_t hz)
{
    hal_spi_standard_init(FLASH_SPI, hz);
}

void boot_flash_read(uint32_t addr, uint8_t *data, size_t len)
{
    uint8_t cmd[4] = {0x03, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    hal_spi_standard_receive(FLASH_SPI, FLASH_SPI_CS, cmd, sizeof(cmd), data, len);
}

void boot_flash_read_id(uint8_t id[3])
{
    uint8_t cmd = 0x9F;
    hal_spi_standard_receive(FLASH_SPI, FLASH_SPI_CS, &cmd, 1, id, 3);
}

static uint8_t boot_flash_read_status(void)
{
    uint8_t cmd = 0x05;
    uint8_t status = 0xFF;
    hal_spi_standard_receive(FLASH_SPI, FLASH_SPI_CS, &cmd, 1, &status, 1);
    return status;
}

static void boot_flash_wait_ready(void)
{
    for(uint32_t i = 0; i < 1000000UL; i++)
    {
        if((boot_flash_read_status() & 0x01U) == 0)
            return;
    }
}

static void boot_flash_write_enable(void)
{
    uint8_t cmd = 0x06;
    boot_flash_cmd(&cmd, 1);
}

void boot_flash_sector_erase(uint32_t addr)
{
    uint8_t cmd[4] = {0x20, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    boot_flash_write_enable();
    boot_flash_cmd(cmd, sizeof(cmd));
    boot_flash_wait_ready();
}

void boot_flash_page_program(uint32_t addr, const uint8_t *data, size_t len)
{
    uint8_t cmd[4] = {0x02, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    boot_flash_write_enable();
    hal_spi_standard_send(FLASH_SPI, FLASH_SPI_CS, cmd, sizeof(cmd), data, len);
    boot_flash_wait_ready();
}
