#include "hal_spi.h"

#include <stddef.h>

#include <encoding.h>
#include <platform.h>
#include <spi.h>
#include <sysctl.h>

#include "hal_time.h"
#include "board_config.h"
#if HK_BOARD_LCD_PARALLEL8
#include <dmac.h>
#include "hal_shared_buffer.h"
/* DMA0 belongs to the Cube display; KPU uses DMA5. One complete LCD
 * transaction is queued before starting, as in the MaixPy DMA transport. */
static uint32_t s_parallel_words[LCD_W * LCD_H] __attribute__((aligned(64)));
static size_t s_parallel_count;
#endif

#define HAL_SPI_FIFO_TIMEOUT_US 500000ULL
#define HAL_SPI_STANDARD_MAX_TIMEOUT_US 2000U

static uint8_t s_fifo_tx_active_mask;
static uint8_t s_fifo_tx_pending_select[4];

static uint64_t deadline_to_cpu_cycles(uint64_t deadline_us)
{
    uint64_t frequency;
    uint64_t seconds;
    uint64_t cycles;

    if(deadline_us == UINT64_MAX)
        return UINT64_MAX;
    frequency = sysctl_clock_get_freq(SYSCTL_CLOCK_CPU);
    seconds = deadline_us / 1000000ULL;
    if(!frequency || seconds > UINT64_MAX / frequency)
        return UINT64_MAX;
    cycles = seconds * frequency;
    deadline_us %= 1000000ULL;
    if(deadline_us > (UINT64_MAX - cycles) / frequency)
        return UINT64_MAX;
    return cycles + deadline_us * frequency / 1000000ULL;
}

static uint8_t cpu_cycle_deadline_reached(uint64_t deadline_cycles)
{
    return (uint8_t)(deadline_cycles != UINT64_MAX &&
                     read_cycle() >= deadline_cycles);
}

static uint8_t fifo_tx_active(uint8_t device)
{
    return (uint8_t)(device < 4U &&
                     (s_fifo_tx_active_mask & (uint8_t)(1U << device)) != 0U);
}

static void fifo_tx_set_active(uint8_t device, uint8_t active)
{
    uint8_t bit = (uint8_t)(1U << device);

    if(active)
        s_fifo_tx_active_mask |= bit;
    else
        s_fifo_tx_active_mask &= (uint8_t)~bit;
}

static volatile spi_t *hal_spi_regs(uint8_t device)
{
    switch(device)
    {
    case 0:
        return (volatile spi_t *)SPI0_BASE_ADDR;
    case 1:
        return (volatile spi_t *)SPI1_BASE_ADDR;
    case 3:
        return (volatile spi_t *)SPI3_BASE_ADDR;
    default:
        return NULL;
    }
}

void hal_spi_init(uint8_t device, uint32_t frame_bits)
{
    spi_init((spi_device_num_t)device, SPI_WORK_MODE_0, SPI_FF_STANDARD, frame_bits, 0);
}

void hal_spi_parallel8_phase(uint8_t device, uint32_t frame_bits, uint8_t command)
{
    volatile spi_t *controller = hal_spi_regs(device);
    uint32_t divider;
    if(!controller)
        return;
    divider = controller->baudr;
    spi_init((spi_device_num_t)device, SPI_WORK_MODE_0,
             SPI_FF_OCTAL, frame_bits, 0);
    spi_init_non_standard((spi_device_num_t)device,
                          command ? 8U : 0U,
                          command ? 0U : frame_bits,
                          0U, SPI_AITM_AS_FRAME_FORMAT);
    if(divider != 0U)
        controller->baudr = divider;
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

void hal_spi_standard_send_segments(uint8_t device, uint8_t chip_select,
                                    const uint8_t *first, size_t first_len,
                                    const uint8_t *second, size_t second_len)
{
    (void)hal_spi_standard_send_segments_checked(device, chip_select, first, first_len,
        second, second_len, HAL_SPI_STANDARD_MAX_TIMEOUT_US);
}

/* A KPU interrupt may execute software layers for longer than the flash TX
 * FIFO can cover. Keep the short wire exchange uninterrupted, not the flash's
 * subsequent millisecond program/erase busy interval. Never enable IRQs that
 * were disabled on entry (and never alter the mie source-enable register). */
static unsigned long standard_irq_save(void)
{
    unsigned long state = clear_csr(mstatus, MSTATUS_MIE);
    __sync_synchronize();
    return state;
}

static void standard_irq_restore(unsigned long state)
{
    __sync_synchronize();
    if(state & MSTATUS_MIE)
        (void)set_csr(mstatus, MSTATUS_MIE);
}

static uint8_t standard_8bit(volatile spi_t *spi, uint8_t device)
{
    return spi && ((spi->ctrlr0 >> (device == 3U ? 0U : 16U)) & 31U) == 7U &&
        ((spi->ctrlr0 >> (device == 3U ? 22U : 21U)) & 3U) == SPI_FF_STANDARD;
}

uint8_t hal_spi_standard_send_segments_checked(uint8_t device, uint8_t chip_select,
    const uint8_t *first, size_t first_len, const uint8_t *second, size_t second_len,
    uint32_t timeout_us)
{
    volatile spi_t *spi = hal_spi_regs(device);
    uint32_t tmod_offset;
    size_t first_pos = 0U;
    size_t second_pos = 0U;
    unsigned long irq_state;
    uint64_t deadline_us;
    uint8_t success = 0U;

    if(!standard_8bit(spi, device) || chip_select >= 4U ||
       (first_len && !first) || (second_len && !second) ||
       (!first_len && !second_len) || first_len > 260U ||
       second_len > 260U - first_len || !timeout_us ||
       timeout_us > HAL_SPI_STANDARD_MAX_TIMEOUT_US)
        return 0U;

    irq_state = standard_irq_save();
    /* Start the wire budget after masking: a KPU ISR before this point must
     * not turn a healthy controller into an already-expired transaction. */
    deadline_us = hal_time_us() + timeout_us;
    tmod_offset = device == 3U ? 10U : 8U;
    spi->ssienr = 0U;
    spi->ser = 0U;
    spi->dmacr = 0U;
    spi->ctrlr0 = (spi->ctrlr0 & ~(3U << tmod_offset)) |
                  ((uint32_t)SPI_TMOD_TRANS << tmod_offset);
    spi->ssienr = 1U;

    while(first_pos < first_len || second_pos < second_len)
    {
        size_t room;
        if(hal_time_us() >= deadline_us)
            goto done;
        room = spi->txflr < 32U ? 32U - spi->txflr : 0U;

        while(room && first_pos < first_len)
        {
            spi->dr[0] = first[first_pos++];
            room--;
        }
        while(room && second_pos < second_len)
        {
            spi->dr[0] = second[second_pos++];
            room--;
        }
        /* Prefill before selecting the chip, including command and payload. */
        spi->ser = 1U << chip_select;
    }

    while((spi->sr & 0x05U) != 0x04U)
        if(hal_time_us() >= deadline_us)
            goto done;
    success = hal_time_us() < deadline_us;
done:
    spi->ser = 0U;
    spi->ssienr = 0U;
    standard_irq_restore(irq_state);
    return success;
}

void hal_spi_standard_receive(uint8_t device, uint8_t chip_select, const uint8_t *cmd, size_t cmd_len, uint8_t *data, size_t data_len)
{
    spi_receive_data_standard((spi_device_num_t)device, (spi_chip_select_t)chip_select, cmd, cmd_len, data, data_len);
}

uint8_t hal_spi_standard_receive_checked(uint8_t device, uint8_t chip_select,
    const uint8_t *cmd, size_t cmd_len, uint8_t *data, size_t data_len,
    uint32_t timeout_us)
{
    volatile spi_t *spi = hal_spi_regs(device);
    unsigned long irq_state;
    uint64_t deadline_us;
    uint8_t success = 0U;
    size_t sent = 0U, received = 0U;
    uint32_t tmod_offset = device == 3U ? 10U : 8U;

    if(!standard_8bit(spi, device) || chip_select >= 4U || !data || !data_len ||
       data_len > 256U || cmd_len > 32U || (cmd_len && !cmd) ||
       !timeout_us || timeout_us > HAL_SPI_STANDARD_MAX_TIMEOUT_US)
        return 0U;
    irq_state = standard_irq_save();
    deadline_us = hal_time_us() + timeout_us;
    spi->ssienr = 0U;
    spi->ser = 0U;
    spi->dmacr = 0U;
    spi->ctrlr0 = (spi->ctrlr0 & ~(3U << tmod_offset)) |
        ((uint32_t)(cmd_len ? SPI_TMOD_EEROM : SPI_TMOD_RECV) << tmod_offset);
    spi->ctrlr1 = (uint32_t)data_len - 1U;
    spi->ssienr = 1U;
    while(sent < cmd_len)
    {
        if(hal_time_us() >= deadline_us)
            goto done;
        if(spi->txflr < 32U)
            spi->dr[0] = cmd[sent++];
    }
    if(!cmd_len)
        spi->dr[0] = 0xFFU;
    spi->ser = 1U << chip_select;
    while(received < data_len)
    {
        size_t available;
        if(hal_time_us() >= deadline_us)
            goto done;
        available = spi->rxflr;
        if(available > data_len - received)
            available = data_len - received;
        while(available--)
            data[received++] = (uint8_t)spi->dr[0];
    }
    while((spi->sr & 0x05U) != 0x04U)
        if(hal_time_us() >= deadline_us)
            goto done;
    success = hal_time_us() < deadline_us;
done:
    spi->ser = 0U;
    spi->ssienr = 0U;
    standard_irq_restore(irq_state);
    return success;
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
    s_fifo_tx_pending_select[device] = 0U;
    fifo_tx_set_active(device, 0U);
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
    spi->ctrlr0 = ((bits - 1U) << 16) |
                  (spi->ctrlr0 & ~(31U << 16));
}

uint8_t hal_spi_fifo_send_bytes_until(uint8_t device, uint8_t chip_select,
                                      const uint8_t *data, size_t len,
                                      uint64_t deadline_us)
{
    if((len && !data) ||
       !hal_spi_fifo_tx_begin_until(device, chip_select, deadline_us))
        return 0U;
    if(len == 0U)
        return hal_spi_fifo_tx_end_until(device, deadline_us);
    if(!hal_spi_fifo_tx_write_until(device, data, len, deadline_us))
    {
        hal_spi_fifo_tx_abort(device);
        return 0U;
    }
    return hal_spi_fifo_tx_end_until(device, deadline_us);
}

uint8_t hal_spi_fifo_tx_begin_until(
    uint8_t device, uint8_t chip_select, uint64_t deadline_us)
{
    volatile spi_t *spi = hal_spi_regs(device);

    if(!spi || device >= 4U || chip_select >= 4U ||
       fifo_tx_active(device) || hal_time_us() >= deadline_us)
        return 0U;
    spi->ssienr = 0U;
    hal_spi_fifo_set_tmod_tx(device);
    while(spi->rxflr)
        (void)spi->dr[0];
    /* Octal data is staged until end, then sent as one DMA transaction. */
    s_fifo_tx_pending_select[device] =
        device == 0U && ((spi->ctrlr0 >> 21) & 3U) == SPI_FF_OCTAL
            ? (uint8_t)(1U << chip_select) : 0U;
#if HK_BOARD_LCD_PARALLEL8
    /* A timed-out DMA must be idle before its staging memory can be reused. */
    if(s_fifo_tx_pending_select[device] && !dmac_is_idle(DMAC_CHANNEL0))
    {
        s_fifo_tx_pending_select[device] = 0U;
        return 0U;
    }
#endif
    spi->ser = s_fifo_tx_pending_select[device] ? 0U : 1U << chip_select;
    spi->ssienr = 1U;
    fifo_tx_set_active(device, 1U);
#if HK_BOARD_LCD_PARALLEL8
    if(s_fifo_tx_pending_select[device])
        s_parallel_count = 0U;
#endif
    return 1U;
}

uint8_t hal_spi_fifo_tx_write_until(
    uint8_t device, const uint8_t *data, size_t len,
    uint64_t deadline_us)
{
    volatile spi_t *spi = hal_spi_regs(device);
    uint64_t deadline_cycles = deadline_to_cpu_cycles(deadline_us);
    size_t i = 0U;

    if(!spi || device >= 4U ||
       !fifo_tx_active(device) || (len && !data))
        return 0U;

#if HK_BOARD_LCD_PARALLEL8
    if(s_fifo_tx_pending_select[device])
    {
        uint32_t *words = hal_shared_buffer_uncached(s_parallel_words);
        if(len > LCD_W * LCD_H - s_parallel_count ||
           cpu_cycle_deadline_reached(deadline_cycles))
        {
            hal_spi_fifo_tx_abort(device);
            return 0U;
        }
        while(i < len)
            words[s_parallel_count++] = data[i++];
        return 1U;
    }
#endif
    while(i < len)
    {
        size_t fifo_len = 32U - spi->txflr;
        if(cpu_cycle_deadline_reached(deadline_cycles))
        {
            hal_spi_fifo_tx_abort(device);
            return 0U;
        }
        if(!fifo_len)
            continue;
        if(fifo_len > len - i)
            fifo_len = len - i;
        while(fifo_len--)
            spi->dr[0] = data[i++];
    }

    return 1U;
}

uint8_t hal_spi_fifo_tx_write_u16be_until(
    uint8_t device, const uint8_t *data, size_t len,
    uint64_t deadline_us)
{
    volatile spi_t *spi = hal_spi_regs(device);
    uint64_t deadline_cycles = deadline_to_cpu_cycles(deadline_us);
    size_t i = 0U;

    if(!spi || device >= 4U || !fifo_tx_active(device) ||
       (len && !data) || (len & 1U))
        return 0U;
#if HK_BOARD_LCD_PARALLEL8
    if(s_fifo_tx_pending_select[device])
    {
        uint32_t *words = hal_shared_buffer_uncached(s_parallel_words);
        if((len / 2U) > LCD_W * LCD_H - s_parallel_count ||
           cpu_cycle_deadline_reached(deadline_cycles))
        {
            hal_spi_fifo_tx_abort(device);
            return 0U;
        }
        while(i < len)
        {
            words[s_parallel_count++] = ((uint32_t)data[i] << 8) | data[i + 1U];
            i += 2U;
        }
        return 1U;
    }
#endif
    while(i < len)
    {
        size_t fifo_len = 32U - spi->txflr;
        if(cpu_cycle_deadline_reached(deadline_cycles))
        {
            hal_spi_fifo_tx_abort(device);
            return 0U;
        }
        if(!fifo_len)
            continue;
        if(fifo_len > (len - i) / 2U)
            fifo_len = (len - i) / 2U;
        while(fifo_len--)
        {
            spi->dr[0] = ((uint32_t)data[i] << 8) | data[i + 1U];
            i += 2U;
        }
    }

    return 1U;
}

uint8_t hal_spi_fifo_tx_end_until(uint8_t device, uint64_t deadline_us)
{
    volatile spi_t *spi = hal_spi_regs(device);

    if(!spi || device >= 4U || !fifo_tx_active(device))
        return 0U;

#if HK_BOARD_LCD_PARALLEL8
    if(s_fifo_tx_pending_select[device] && s_parallel_count)
    {
        /* Exclusive DMA0 is idle before the SDK configuration helper, whose
         * internal disable/idle check therefore cannot wait for a transfer. */
        if(!dmac_is_idle(DMAC_CHANNEL0) || hal_time_us() >= deadline_us)
            goto timeout;
        spi->dmacr = 2U;
        sysctl_dma_select(SYSCTL_DMA_CHANNEL_0, SYSCTL_DMA_SELECT_SSI0_TX_REQ);
        __sync_synchronize();
        dmac_set_single_mode(DMAC_CHANNEL0,
            hal_shared_buffer_uncached(s_parallel_words), (void *)&spi->dr[0],
            DMAC_ADDR_INCREMENT, DMAC_ADDR_NOCHANGE,
            DMAC_MSIZE_4, DMAC_TRANS_WIDTH_32, s_parallel_count);
        spi->ser = s_fifo_tx_pending_select[device];
        while(!dmac_is_idle(DMAC_CHANNEL0))
        {
            if(hal_time_us() >= deadline_us)
                goto timeout;
        }
    }
#endif
    while((spi->sr & 0x05U) != 0x04U)
    {
        if(hal_time_us() >= deadline_us)
            goto timeout;
    }
    if(hal_time_us() > deadline_us)
        goto timeout;
    spi->ser = 0;
    spi->ssienr = 0;
#if HK_BOARD_LCD_PARALLEL8
    if(s_fifo_tx_pending_select[device])
        spi->dmacr = 0U;
#endif
    s_fifo_tx_pending_select[device] = 0U;
    fifo_tx_set_active(device, 0U);
    return 1U;

timeout:
    hal_spi_fifo_tx_abort(device);
    return 0U;
}

void hal_spi_fifo_tx_abort(uint8_t device)
{
    volatile spi_t *spi = hal_spi_regs(device);

    if(!spi || device >= 4U)
        return;
#if HK_BOARD_LCD_PARALLEL8
    if(s_fifo_tx_pending_select[device])
    {
        spi->dmacr = 0U;
        dmac_channel_disable(DMAC_CHANNEL0);
        s_parallel_count = 0U;
    }
#endif
    spi->ser = 0U;
    spi->ssienr = 0U;
    s_fifo_tx_pending_select[device] = 0U;
    fifo_tx_set_active(device, 0U);
}

uint8_t hal_spi_fifo_send_bytes(uint8_t device, uint8_t chip_select,
                                const uint8_t *data, size_t len)
{
    uint64_t now = hal_time_us();
    uint64_t deadline = UINT64_MAX - now < HAL_SPI_FIFO_TIMEOUT_US
                            ? UINT64_MAX
                            : now + HAL_SPI_FIFO_TIMEOUT_US;

    return hal_spi_fifo_send_bytes_until(device, chip_select, data, len,
                                         deadline);
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
