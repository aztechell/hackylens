#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "encoding.h"
#include "spi.h"
#include "hal_spi.h"

spi_t cube_spi_registers[4];
unsigned long spi_test_mstatus;
unsigned long spi_test_mhartid;
static uint64_t now;
static unsigned masked_steps;
static uint8_t simulate_rx;
static uint8_t clock_started;
static uint64_t entry_delay;

uint64_t hal_time_us(void)
{
    spi_t *spi = &cube_spi_registers[3];
    if(!clock_started)
    {
        /* The first timestamp must be after IRQ-save. Model a long KPU ISR
         * just before that save; it cannot consume the relative wire budget. */
        assert(!(spi_test_mstatus & MSTATUS_MIE));
        now += entry_delay;
        clock_started = 1U;
    }
    if(spi->ssienr)
    {
        /* Every progress/deadline check inside the actual wire exchange must
         * happen without KPU IRQ preemption, including receive and drain. */
        assert(!(spi_test_mstatus & MSTATUS_MIE));
        masked_steps++;
        if(simulate_rx && spi->ser && ((spi->ctrlr0 >> 10) & 3U) >= 2U)
        {
            spi->rxflr = spi->ctrlr1 + 1U;
            spi->dr[0] = 0xA5U;
        }
    }
    return ++now;
}
void spi_init(unsigned device, unsigned mode, unsigned format, unsigned bits, unsigned endian)
{ (void)device; (void)mode; (void)format; (void)bits; (void)endian; }
void spi_init_non_standard(unsigned device, unsigned ins, unsigned addr, unsigned wait, unsigned mode)
{ (void)device; (void)ins; (void)addr; (void)wait; (void)mode; }
uint32_t spi_set_clk_rate(unsigned device, uint32_t rate) { (void)device; return rate; }
void spi_send_data_standard(unsigned device, unsigned cs, const uint8_t *cmd,
    size_t cmd_len, const uint8_t *data, size_t len)
{ (void)device; (void)cs; (void)cmd; (void)cmd_len; (void)data; (void)len; assert(0); }
void spi_receive_data_standard(unsigned device, unsigned cs, const uint8_t *cmd,
    size_t cmd_len, uint8_t *data, size_t len)
{ (void)device; (void)cs; (void)cmd; (void)cmd_len; (void)data; (void)len; assert(0); }
int dmac_is_idle(unsigned channel) { (void)channel; assert(0); return 1; }
void dmac_channel_disable(unsigned channel) { (void)channel; assert(0); }
void dmac_set_single_mode(unsigned ch, const void *src, void *dst, unsigned si,
    unsigned di, unsigned burst, unsigned width, size_t count)
{ (void)ch; (void)src; (void)dst; (void)si; (void)di; (void)burst; (void)width; (void)count; assert(0); }

static void reset(unsigned long status)
{
    memset(cube_spi_registers, 0, sizeof(cube_spi_registers));
    cube_spi_registers[3].ctrlr0 = 7U; /* SPI3 DFS at bit 0, TMOD at bit 10. */
    cube_spi_registers[3].sr = 4U;
    cube_spi_registers[0].ctrlr0 = 0x12345678U;
    spi_test_mstatus = status;
    now = 0U;
    masked_steps = 0U;
    simulate_rx = 0U;
    clock_started = 0U;
    entry_delay = 0U;
}
static void closed(unsigned long status)
{
    assert(spi_test_mstatus == status);
    assert(!cube_spi_registers[3].ssienr && !cube_spi_registers[3].ser);
    assert(!cube_spi_registers[3].dmacr);
    assert(cube_spi_registers[0].ctrlr0 == 0x12345678U);
    assert(now < entry_delay + 100U);
}
int main(void)
{
    uint8_t command[4] = {2U, 0U, 0U, 0U};
    uint8_t data[256];
    memset(data, 0x5AU, sizeof(data));
    for(unsigned enabled = 0; enabled < 2U; enabled++)
    {
        unsigned long status = 0x1880U | (enabled ? MSTATUS_MIE : 0U);
        reset(status);
        memset(data, 0x5AU, sizeof(data));
        entry_delay = 10000U;
        assert(hal_spi_standard_send_segments_checked(3U, 0U,
            command, sizeof(command), data, sizeof(data), 50U));
        assert(masked_steps >= 9U && cube_spi_registers[3].dr[0] == 0x5AU);
        assert((cube_spi_registers[3].ctrlr0 & 31U) == 7U);
        assert(((cube_spi_registers[3].ctrlr0 >> 10) & 3U) == SPI_TMOD_TRANS);
        closed(status);

        reset(status);
        cube_spi_registers[3].txflr = 32U;
        assert(!hal_spi_standard_send_segments_checked(3U, 0U,
            command, sizeof(command), data, sizeof(data), 10U));
        closed(status);

        reset(status);
        cube_spi_registers[3].sr = 1U; /* FIFO drains but hardware stays busy. */
        assert(!hal_spi_standard_send_segments_checked(3U, 0U,
            command, sizeof(command), data, sizeof(data), 20U));
        closed(status);

        reset(status);
        simulate_rx = 1U;
        memset(data, 0, sizeof(data));
        assert(hal_spi_standard_receive_checked(3U, 0U,
            command, sizeof(command), data, sizeof(data), 30U));
        for(unsigned i = 0; i < sizeof(data); i++) assert(data[i] == 0xA5U);
        closed(status);

        reset(status); /* Receive FIFO never supplies data. */
        assert(!hal_spi_standard_receive_checked(3U, 0U, command, 1U, data, 1U, 10U));
        closed(status);

        reset(status);
        cube_spi_registers[3].txflr = 32U;
        assert(!hal_spi_standard_receive_checked(3U, 0U, command, 1U, data, 1U, 10U));
        closed(status);

        reset(status);
        simulate_rx = 1U;
        cube_spi_registers[3].sr = 1U;
        assert(!hal_spi_standard_receive_checked(3U, 0U, command, 1U, data, 1U, 15U));
        closed(status);

        reset(status);
        assert(!hal_spi_standard_send_segments_checked(3U, 0U,
            command, sizeof(command), data, sizeof(data), 0U));
        assert(!masked_steps && !clock_started); /* Invalid budget: no mutation. */
        closed(status);
        assert(!hal_spi_standard_receive_checked(3U, 0U, command, 1U, data, 257U, 50U));
        assert(!hal_spi_standard_send_segments_checked(3U, 0U, command, 4U, data, 257U, 50U));
        closed(status);
    }
    puts("FLASH_PIO_OK");
    return 0;
}
