#include <assert.h>
#include <stdio.h>
#include "spi.h"
#include "hal_spi.h"

spi_t cube_spi_registers[4];
static unsigned instruction_bits;
static unsigned address_bits;
static unsigned transfer_mode;
static unsigned frame_format;

static unsigned dma_hang, dma_busy, dma_calls, dma_disables;
static uint32_t captured[240U * 240U];
static size_t captured_count;
static uint64_t fake_time = 1U;
uint64_t hal_time_us(void) { return dma_busy ? ++fake_time : fake_time; }
int dmac_is_idle(unsigned channel) { assert(channel == 0U); return !dma_busy; }
void dmac_channel_disable(unsigned channel)
{ assert(channel == 0U); dma_busy = 0U; dma_disables++; }
void dmac_set_single_mode(unsigned channel, const void *src, void *dest,
                         unsigned src_inc, unsigned dest_inc, unsigned burst,
                         unsigned width, size_t count)
{
    const uint32_t *words = src;
    assert(channel == 0U && src_inc == 0U && dest_inc == 1U);
    assert(burst == 1U && width == 2U);
    assert(dest == (void *)&cube_spi_registers[0].dr[0]);
    assert(cube_spi_registers[0].ser == 0U);
    assert(cube_spi_registers[0].dmacr == 2U);
    captured_count = count;
    for(size_t i = 0U; i < count; ++i) captured[i] = words[i];
    dma_calls++;
    dma_busy = dma_hang;
}
void spi_init(unsigned device, unsigned mode, unsigned format,
              unsigned bits, unsigned endian)
{
    assert(device == 0U && mode == SPI_WORK_MODE_0 && endian == 0U);
    frame_format = format;
    cube_spi_registers[device].ctrlr0 = ((bits - 1U) << 16) | (format << 21);
    cube_spi_registers[device].baudr = 0x14U;
}
void spi_init_non_standard(unsigned device, unsigned instruction, unsigned address,
                           unsigned wait, unsigned mode)
{
    assert(device == 0U && wait == 0U);
    instruction_bits = instruction;
    address_bits = address;
    transfer_mode = mode;
}
uint32_t spi_set_clk_rate(unsigned device, uint32_t rate)
{
    (void)device;
    return rate;
}
void spi_send_data_standard(unsigned device, unsigned chip,
    const uint8_t *cmd, size_t cmd_size, const uint8_t *data, size_t data_size)
{
    (void)device; (void)chip; (void)cmd; (void)cmd_size; (void)data; (void)data_size;
}
void spi_receive_data_standard(unsigned device, unsigned chip,
    const uint8_t *cmd, size_t cmd_size, uint8_t *data, size_t data_size)
{
    (void)device; (void)chip; (void)cmd; (void)cmd_size; (void)data; (void)data_size;
}

int main(void)
{
    cube_spi_registers[0].baudr = 6U;
    hal_spi_parallel8_phase(0U, 8U, 1U);
    assert(frame_format == SPI_FF_OCTAL);
    assert(instruction_bits == 8U && address_bits == 0U);
    assert(transfer_mode == SPI_AITM_AS_FRAME_FORMAT);
    assert(cube_spi_registers[0].baudr == 6U);
    hal_spi_parallel8_phase(0U, 8U, 0U);
    assert(instruction_bits == 0U && address_bits == 8U);
    hal_spi_parallel8_phase(0U, 16U, 0U);
    assert(instruction_bits == 0U && address_bits == 16U);
    assert(cube_spi_registers[0].baudr == 6U);
    hal_spi_fifo_set_tmod_tx(0U);
    hal_spi_fifo_set_frame_bits(0U, 8U);
    assert((cube_spi_registers[0].ctrlr0 & (3U << 21)) == (SPI_FF_OCTAL << 21));
    assert((cube_spi_registers[0].ctrlr0 & (3U << 8)) == (1U << 8));
    assert((cube_spi_registers[0].ctrlr0 & (31U << 16)) == (7U << 16));
    cube_spi_registers[0].sr = 4U;
    hal_spi_parallel8_phase(0U, 16U, 0U);
    assert(hal_spi_fifo_tx_begin_until(0U, 3U, 100U));
    assert(cube_spi_registers[0].ser == 0U);
    {
        const uint8_t pixels[] = {0xF8U, 0x00U, 0x07U, 0xE0U};
        assert(hal_spi_fifo_tx_write_u16be_until(0U, pixels, sizeof(pixels), 100U));
        assert(cube_spi_registers[0].ser == 0U);
    }
    assert(hal_spi_fifo_tx_end_until(0U, 100U));
    assert(dma_calls == 1U && captured_count == 2U);
    assert(captured[0] == 0xF800U && captured[1] == 0x07E0U);
    assert(cube_spi_registers[0].dmacr == 0U);
    assert(cube_spi_registers[0].ser == 0U);
    assert(hal_spi_fifo_tx_begin_until(0U, 3U, 100U));
    hal_spi_fifo_tx_abort(0U);
    assert(cube_spi_registers[0].ser == 0U);
    {
        const uint8_t command = 0x2AU;
        unsigned previous_calls = dma_calls;
        hal_spi_parallel8_phase(0U, 8U, 1U);
        assert(hal_spi_fifo_send_bytes_until(0U, 3U, &command, 1U, 100U));
        assert(dma_calls == previous_calls + 1U && captured_count == 1U);
        assert(captured[0] == 0x2AU);
        dma_hang = 1U;
        assert(!hal_spi_fifo_send_bytes_until(0U, 3U, &command, 1U, 5U));
        assert(dma_disables > 0U && !dma_busy);
        assert(cube_spi_registers[0].ser == 0U);
        assert(cube_spi_registers[0].ssienr == 0U);
        dma_hang = 0U;
        assert(hal_spi_fifo_send_bytes_until(0U, 3U, &command, 1U, 100U));
    }
    {
        static uint8_t frame[240U * 240U * 2U];
        const uint8_t extra[] = {0x12U, 0x34U};
        unsigned previous_calls = dma_calls;
        frame[0] = 0xF8U;
        frame[sizeof(frame) - 2U] = 0x07U;
        frame[sizeof(frame) - 1U] = 0xE0U;
        hal_spi_parallel8_phase(0U, 16U, 0U);
        assert(hal_spi_fifo_tx_begin_until(0U, 3U, 100U));
        assert(hal_spi_fifo_tx_write_u16be_until(0U, frame, 480U, 100U));
        assert(hal_spi_fifo_tx_write_u16be_until(0U, frame + 480U,
                                               sizeof(frame) - 480U, 100U));
        assert(dma_calls == previous_calls);
        assert(hal_spi_fifo_tx_end_until(0U, 100U));
        assert(captured_count == 240U * 240U);
        assert(captured[0] == 0xF800U && captured[captured_count - 1U] == 0x07E0U);
        assert(hal_spi_fifo_tx_begin_until(0U, 3U, 100U));
        assert(hal_spi_fifo_tx_write_u16be_until(0U, frame, sizeof(frame), 100U));
        assert(!hal_spi_fifo_tx_write_u16be_until(0U, extra, sizeof(extra), 100U));
        assert(cube_spi_registers[0].ser == 0U && cube_spi_registers[0].ssienr == 0U);
    }
    hal_spi_init(0U, 8U);
    assert(hal_spi_fifo_tx_begin_until(0U, 3U, 100U));
    assert(cube_spi_registers[0].ser == 8U);
    hal_spi_fifo_tx_abort(0U);
    puts("CUBE_SPI_OK command8 data8 pixel16 preserve-clock octal-frame-format");
    return 0;
}
