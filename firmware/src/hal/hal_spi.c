#include "hal_spi.h"

#include <stddef.h>

#include <platform.h>
#include <spi.h>
#include <sysctl.h>

static volatile spi_t *hal_spi_regs(uint8_t device)
{
    switch(device)
    {
    case 0:
        return (volatile spi_t *)SPI0_BASE_ADDR;
    case 1:
        return (volatile spi_t *)SPI1_BASE_ADDR;
    default:
        return NULL;
    }
}

void hal_spi_init(uint8_t device, uint32_t frame_bits)
{
    spi_init((spi_device_num_t)device, SPI_WORK_MODE_0, SPI_FF_STANDARD, frame_bits, 0);
}

uint32_t hal_spi_set_clock(uint8_t device, uint32_t hz)
{
    return spi_set_clk_rate((spi_device_num_t)device, hz);
}

void hal_spi0_enable_dvp_data(void)
{
    sysctl_set_spi0_dvp_data(1);
}

void hal_spi_standard_init(uint8_t device, uint32_t hz)
{
    hal_spi_init(device, 8);
    hal_spi_set_clock(device, hz);
}

void hal_spi_standard_send(uint8_t device, uint8_t chip_select, const uint8_t *cmd, size_t cmd_len, const uint8_t *data, size_t data_len)
{
    spi_send_data_standard((spi_device_num_t)device, (spi_chip_select_t)chip_select, cmd, cmd_len, data, data_len);
}

void hal_spi_standard_receive(uint8_t device, uint8_t chip_select, const uint8_t *cmd, size_t cmd_len, uint8_t *data, size_t data_len)
{
    spi_receive_data_standard((spi_device_num_t)device, (spi_chip_select_t)chip_select, cmd, cmd_len, data, data_len);
}

void hal_spi_fifo_config(uint8_t device, uint32_t hz, uint8_t chip_select)
{
    volatile spi_t *spi = hal_spi_regs(device);
    if(!spi)
        return;

    hal_spi_init(device, 8);
    hal_spi_set_clock(device, hz);
    spi->ssienr = 0;
    spi->ctrlr0 = (spi->ctrlr0 & ~(3U << 8)) | (0U << 8);
    spi->ctrlr1 = 0;
    spi->ser = 1U << chip_select;
    while(spi->rxflr)
        (void)spi->dr[0];
    spi->ssienr = 1;
}

void hal_spi_fifo_set_tmod_tx(uint8_t device)
{
    volatile spi_t *spi = hal_spi_regs(device);
    if(!spi)
        return;
    spi->ctrlr0 = (spi->ctrlr0 & ~(3U << 8)) | (1U << 8);
}

void hal_spi_fifo_set_frame_bits(uint8_t device, uint32_t bits)
{
    volatile spi_t *spi = hal_spi_regs(device);
    if(!spi || bits == 0)
        return;
    spi->ctrlr0 = ((bits - 1U) << 16) | (spi->ctrlr0 & 0x1FU);
}

void hal_spi_fifo_send_bytes(uint8_t device, uint8_t chip_select, const uint8_t *data, size_t len)
{
    volatile spi_t *spi = hal_spi_regs(device);
    size_t i = 0;
    if(!spi || len == 0)
        return;

    hal_spi_fifo_set_tmod_tx(device);
    spi->ssienr = 1;
    spi->ser = 1U << chip_select;

    while(i < len)
    {
        size_t fifo_len = 32U - spi->txflr;
        if(fifo_len > len - i)
            fifo_len = len - i;
        while(fifo_len--)
            spi->dr[0] = data[i++];
    }

    while((spi->sr & 0x05U) != 0x04U)
        ;
    spi->ser = 0;
    spi->ssienr = 0;
}

uint8_t hal_spi_fifo_xfer_u8(uint8_t device, uint8_t out)
{
    volatile spi_t *spi = hal_spi_regs(device);
    if(!spi)
        return 0xFF;

    while(spi->txflr >= 32)
        ;
    spi->dr[0] = out;
    while(spi->rxflr == 0)
        ;
    return (uint8_t)spi->dr[0];
}
