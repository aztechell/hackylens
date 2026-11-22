#include "hal_external_link.h"
#include "hal_shared_buffer.h"
#include "hal_time.h"

#if defined(HAL_EXTERNAL_LINK_TESTING)
#include "hal_external_link_test_platform.h"
#else
#include <dmac.h>
#include <encoding.h>
#include <i2c.h>
#include <platform.h>
#include <plic.h>
#include <sysctl.h>
#include <uart.h>

#include "../../../firmware/src/internal/hk_board_port.h"
#endif

#if defined(HAL_EXTERNAL_LINK_TESTING)
#define PREPARE_EXTERNAL_UART() board_external_link_uart_pins()
#define PREPARE_EXTERNAL_I2C() board_external_link_i2c_pins()
#else
#define PREPARE_EXTERNAL_UART() hk_board_ops.external_uart_prepare()
#define PREPARE_EXTERNAL_I2C() hk_board_ops.external_i2c_prepare()
#endif

/* K210 general-purpose UART1/2/3 expose an 8-byte TX FIFO. */
#define HAL_EXTERNAL_UART_FIFO_DEPTH 8U
/* UART TX and I2C target RX are mutually exclusive users of DMA1. */
#define HAL_EXTERNAL_LINK_DMA DMAC_CHANNEL1
#define HAL_EXTERNAL_UART_RX_DMA DMAC_CHANNEL2
#define HAL_EXTERNAL_UART_BATCH 32U
#define HAL_EXTERNAL_UART_RX_WORDS 257U
#define HAL_EXTERNAL_UART_BITS_PER_BYTE 10U /* 8N1, including start and stop. */
#define HAL_EXTERNAL_UART_LSR_OVERRUN (1U << 1U)
#define HAL_EXTERNAL_I2C_IRQ_PRIORITY 4U
#define HAL_EXTERNAL_I2C_FIFO_DEPTH 8U
/* One 256-byte target write plus room to identify an overlong transaction. */
#define HAL_EXTERNAL_I2C_DMA_WORDS 272U
#define HAL_EXTERNAL_DMA_STOP_US 100U
#if defined(HAL_EXTERNAL_LINK_TESTING)
#define HAL_EXTERNAL_UART_ADAPTER (uart[UART_DEVICE_1])
#else
#define HAL_EXTERNAL_UART_ADAPTER ((volatile uart_t *)UART1_BASE_ADDR)
#endif

static const hal_external_i2c_callbacks_t *g_i2c_callbacks;
static uint8_t g_i2c_active;
static uint8_t g_i2c_rx_bank;
static uint8_t g_i2c_rx_dma_failed;
static _Alignas(64) uint32_t g_i2c_rx_words[2][HAL_EXTERNAL_I2C_DMA_WORDS];
static const uint8_t *g_i2c_controller_tx;
static uint8_t *g_i2c_controller_rx;
static uint32_t g_i2c_controller_tx_size;
static uint32_t g_i2c_controller_rx_size;
static volatile uint32_t g_i2c_controller_commands;
static volatile uint32_t g_i2c_controller_rx_received;
static volatile uint8_t g_i2c_controller_aborted;
static uint8_t g_i2c_controller_active;
static hal_external_uart_receive_fn g_uart_receive;
static void *g_uart_receive_context;
static uint8_t g_uart_active;
static uint32_t g_uart_baud;
static uint8_t g_uart_rx_bank;
static _Alignas(64) uint32_t g_uart_rx_words[2][HAL_EXTERNAL_UART_RX_WORDS];
static uint8_t g_uart_tx_pending;
static uint64_t g_uart_tx_drained_at;
static _Alignas(64) uint32_t g_uart_tx_words[HAL_EXTERNAL_UART_BATCH];

#if defined(HAL_EXTERNAL_LINK_TESTING)
#define I2C_CONTROLLER_WRITE(adapter, value) \
    hal_external_i2c_test_write_command((value))
#define I2C_READ_DATA(adapter) \
    hal_external_i2c_test_read_data()
#define UART_RX_IRQ_SAVE() 0UL
#define UART_RX_IRQ_RESTORE(state) ((void)(state))
#else
#define I2C_CONTROLLER_WRITE(adapter, value) ((adapter)->data_cmd = (value))
#define I2C_READ_DATA(adapter) ((adapter)->data_cmd)
#define UART_RX_IRQ_SAVE() clear_csr(mstatus, MSTATUS_MIE)
#define UART_RX_IRQ_RESTORE(state) do { if((state) & MSTATUS_MIE) (void)set_csr(mstatus, MSTATUS_MIE); } while(0)
#endif

static void i2c_controller_abort(volatile i2c_t *adapter)
{
    g_i2c_controller_aborted = 1U;
    adapter->intr_mask = 0U;
}

static void i2c_controller_drain_rx(volatile i2c_t *adapter)
{
    uint32_t received = g_i2c_controller_rx_received;
    uint32_t drained = 0U;

    while(adapter->rxflr != 0U &&
          received < g_i2c_controller_rx_size &&
          drained < HAL_EXTERNAL_I2C_FIFO_DEPTH)
    {
        g_i2c_controller_rx[received] =
            (uint8_t)I2C_READ_DATA(adapter);
        __sync_synchronize();
        g_i2c_controller_rx_received = ++received;
        ++drained;
    }
    if(adapter->rxflr != 0U)
        i2c_controller_abort(adapter);
}

static void i2c_controller_fill_tx(volatile i2c_t *adapter)
{
    uint32_t commands = g_i2c_controller_commands;
    uint32_t total = g_i2c_controller_tx_size +
                     g_i2c_controller_rx_size;

    while(adapter->txflr < HAL_EXTERNAL_I2C_FIFO_DEPTH &&
          commands < total)
    {
        uint32_t command = commands < g_i2c_controller_tx_size ?
            I2C_DATA_CMD_DATA(g_i2c_controller_tx[commands]) :
            I2C_DATA_CMD_CMD;

        I2C_CONTROLLER_WRITE(adapter, command);
        __sync_synchronize();
        g_i2c_controller_commands = ++commands;
    }
    if(commands == total)
        adapter->intr_mask &= ~I2C_INTR_MASK_TX_EMPTY;
}

static int i2c_controller_irq(void *context)
{
    volatile i2c_t *adapter = i2c[I2C_DEVICE_0];
    uint32_t status = adapter->intr_stat;

    (void)context;
    if(!g_i2c_controller_active)
        return 0;
    if((status & I2C_INTR_STAT_TX_ABRT) != 0U)
    {
        (void)adapter->clr_tx_abrt;
        i2c_controller_abort(adapter);
        return 0;
    }
    if((status & I2C_INTR_STAT_RX_OVER) != 0U)
    {
        (void)adapter->clr_rx_over;
        i2c_controller_abort(adapter);
        return 0;
    }
    if((status & I2C_INTR_STAT_RX_FULL) != 0U ||
       (status & I2C_INTR_STAT_STOP_DET) != 0U)
        i2c_controller_drain_rx(adapter);
    if(!g_i2c_controller_aborted &&
       (status & I2C_INTR_STAT_TX_EMPTY) != 0U)
        i2c_controller_fill_tx(adapter);
    if((status & I2C_INTR_STAT_STOP_DET) != 0U)
        (void)adapter->clr_stop_det;
    return 0;
}

static uint8_t external_dma_start_rx(dmac_channel_number_t channel,
                                     const volatile uint32_t *source,
                                     uint32_t *words, size_t count)
{
    if(!dmac_is_idle(channel))
        return 0U;
    __sync_synchronize();
    dmac_set_single_mode(channel, (const void *)source, words,
                         DMAC_ADDR_NOCHANGE, DMAC_ADDR_INCREMENT, DMAC_MSIZE_1,
                         DMAC_TRANS_WIDTH_32, count);
    return 1U;
}

static uint8_t external_dma_stop(dmac_channel_number_t channel, uint64_t deadline)
{
    if(!dmac_is_idle(channel))
    {
        /* CHEN has per-channel abort and abort-write-enable fields. */
        const uint64_t bit = UINT64_C(1) << channel;
        dmac->chen = (bit << 32U) | (bit << 40U);
        dmac_channel_disable(channel);
    }
    while(!dmac_is_idle(channel) && hal_time_us() < deadline)
        ;
    return dmac_is_idle(channel);
}

static void i2c_target_notify(hal_external_i2c_event_t event)
{
    if(g_i2c_callbacks && g_i2c_callbacks->event)
        g_i2c_callbacks->event(event);
}

static void i2c_target_start_rx(volatile i2c_t *adapter)
{
    uint32_t *words = hal_shared_buffer_uncached(g_i2c_rx_words[g_i2c_rx_bank]);

    if(!external_dma_start_rx(HAL_EXTERNAL_LINK_DMA, &adapter->data_cmd,
                              words, HAL_EXTERNAL_I2C_DMA_WORDS))
    {
        g_i2c_rx_dma_failed = 1U;
        return;
    }
    adapter->dma_cr = I2C_DMA_CR_RDMAE;
}

static void i2c_target_finish_rx(volatile i2c_t *adapter)
{
    volatile uint32_t *words = hal_shared_buffer_uncached(g_i2c_rx_words[g_i2c_rx_bank]);
    uint64_t deadline;
    uint8_t failed = g_i2c_rx_dma_failed;

    /* Short writes may be buffered in DMA even with an empty peripheral FIFO.
       Finish the DMA block at STOP before interpreting the SRAM contents. */
    deadline = hal_time_us() + HAL_EXTERNAL_DMA_STOP_US;
    while(adapter->rxflr && !dmac_is_idle(HAL_EXTERNAL_LINK_DMA) && hal_time_us() < deadline)
        ;
    adapter->dma_cr = 0U;
    if(!external_dma_stop(HAL_EXTERNAL_LINK_DMA, deadline))
    {
        g_i2c_rx_dma_failed = 1U;
        i2c_target_notify(HAL_EXTERNAL_I2C_EVENT_OVERFLOW);
        return;
    }
    failed |= adapter->rxflr != 0U;
    if(failed)
    {
        for(size_t count = 0U; count < HAL_EXTERNAL_I2C_FIFO_DEPTH && adapter->rxflr; ++count)
            (void)I2C_READ_DATA(adapter);
        i2c_target_notify(HAL_EXTERNAL_I2C_EVENT_OVERFLOW);
    }
    /* Arm the other buffer before passing bytes to the protocol. The DMA DAR
       advances in bursts, so use the first untouched word as the frame end. */
    g_i2c_rx_bank ^= 1U;
    g_i2c_rx_dma_failed = 0U;
    i2c_target_start_rx(adapter);
    for(size_t index = 0U; index < HAL_EXTERNAL_I2C_DMA_WORDS; ++index)
    {
        uint32_t word = words[index];
        if(word == UINT32_MAX)
            break;
        words[index] = UINT32_MAX;
        if(!failed && g_i2c_callbacks && g_i2c_callbacks->receive)
            g_i2c_callbacks->receive((uint8_t)word);
    }
}

static int i2c_target_irq(void *context)
{
    volatile i2c_t *adapter = i2c[I2C_DEVICE_0];
    uint32_t status = adapter->intr_stat;

    (void)context;
    if(status & I2C_INTR_STAT_RX_OVER)
    {
        g_i2c_rx_dma_failed = 1U;
        (void)adapter->clr_rx_over;
    }
    if(status & I2C_INTR_STAT_START_DET)
    {
        (void)adapter->clr_start_det;
        i2c_target_notify(HAL_EXTERNAL_I2C_EVENT_START);
    }
    if(status & I2C_INTR_STAT_STOP_DET)
    {
        (void)adapter->clr_stop_det;
        i2c_target_finish_rx(adapter);
        i2c_target_notify(HAL_EXTERNAL_I2C_EVENT_STOP);
    }
    if(status & I2C_INTR_STAT_RD_REQ)
    {
        adapter->data_cmd = g_i2c_callbacks && g_i2c_callbacks->transmit ?
            g_i2c_callbacks->transmit() : 0U;
        (void)adapter->clr_rd_req;
    }
    return 0;
}

void hal_external_uart_stop(void)
{
    if(!g_uart_active)
        return;
    (void)external_dma_stop(HAL_EXTERNAL_UART_RX_DMA, hal_time_us() + HAL_EXTERNAL_DMA_STOP_US);
    (void)external_dma_stop(HAL_EXTERNAL_LINK_DMA, hal_time_us() + HAL_EXTERNAL_DMA_STOP_US);
    g_uart_active = 0U;
    g_uart_receive = NULL;
    g_uart_receive_context = NULL;
    g_uart_tx_pending = 0U;
    g_uart_tx_drained_at = 0U;
    uart_init(UART_DEVICE_1);
}

void hal_external_uart_init(uint32_t baud,
                            hal_external_uart_receive_fn receive,
                            void *context)
{
    hal_external_i2c_stop();
    hal_external_uart_stop();
    PREPARE_EXTERNAL_UART();
    uart_init(UART_DEVICE_1);
    uart_configure(UART_DEVICE_1, baud, UART_BITWIDTH_8BIT, UART_STOP_1, UART_PARITY_NONE);
    g_uart_receive = receive;
    g_uart_receive_context = context;
    sysctl_dma_select((sysctl_dma_channel_t)HAL_EXTERNAL_LINK_DMA,
                     SYSCTL_DMA_SELECT_UART1_TX_REQ);
    sysctl_dma_select((sysctl_dma_channel_t)HAL_EXTERNAL_UART_RX_DMA,
                     SYSCTL_DMA_SELECT_UART1_RX_REQ);
    g_uart_rx_bank = 0U;
    for(size_t bank = 0U; bank < 2U; ++bank)
    {
        uint32_t *words = hal_shared_buffer_uncached(g_uart_rx_words[bank]);
        for(size_t index = 0U; index < HAL_EXTERNAL_UART_RX_WORDS; ++index)
            words[index] = UINT32_MAX;
    }
    (void)external_dma_start_rx(HAL_EXTERNAL_UART_RX_DMA, &HAL_EXTERNAL_UART_ADAPTER->RBR,
        hal_shared_buffer_uncached(g_uart_rx_words[g_uart_rx_bank]), HAL_EXTERNAL_UART_RX_WORDS);
    g_uart_baud = baud;
    g_uart_active = 1U;
}

uint8_t hal_external_uart_collect_rx(void)
{
    volatile uart_t *adapter = HAL_EXTERNAL_UART_ADAPTER;
    volatile uint32_t *words;
    unsigned long interrupts;
    uint8_t started;
    uint8_t overrun;

    if(!g_uart_active)
        return 0U;

    /* Stopping flushes partial DMA bursts. Arm the other bank before a slow
       interrupt can keep the UART without a DMA consumer. */
    interrupts = UART_RX_IRQ_SAVE();
    overrun = (adapter->LSR & HAL_EXTERNAL_UART_LSR_OVERRUN) != 0U;
    if(!external_dma_stop(HAL_EXTERNAL_UART_RX_DMA, hal_time_us() + HAL_EXTERNAL_DMA_STOP_US))
    {
        UART_RX_IRQ_RESTORE(interrupts);
        return 0U;
    }
    words = hal_shared_buffer_uncached(g_uart_rx_words[g_uart_rx_bank]);
    g_uart_rx_bank ^= 1U;
    started = external_dma_start_rx(HAL_EXTERNAL_UART_RX_DMA, &adapter->RBR,
        hal_shared_buffer_uncached(g_uart_rx_words[g_uart_rx_bank]), HAL_EXTERNAL_UART_RX_WORDS);
    overrun |= (adapter->LSR & HAL_EXTERNAL_UART_LSR_OVERRUN) != 0U;
    UART_RX_IRQ_RESTORE(interrupts);

    /* The last word is reserved to recognize RX beyond the 256-byte window. */
    if(!started || overrun || words[HAL_EXTERNAL_UART_RX_WORDS - 1U] != UINT32_MAX)
        return 0U;
    for(size_t index = 0U; index < HAL_EXTERNAL_UART_RX_WORDS; ++index)
    {
        uint32_t word = words[index];
        if(word == UINT32_MAX)
            break;
        words[index] = UINT32_MAX;
        if(g_uart_receive)
            g_uart_receive(g_uart_receive_context, (uint8_t)word);
    }
    return 1U;
}

size_t hal_external_uart_send_ready(const uint8_t *data, size_t len)
{
    volatile uart_t *adapter = HAL_EXTERNAL_UART_ADAPTER;
    uint32_t *words;
    size_t count;

    if(!data || len == 0U || !g_uart_active || !dmac_is_idle(HAL_EXTERNAL_LINK_DMA))
        return 0U;
    count = len < HAL_EXTERNAL_UART_BATCH ? len : HAL_EXTERNAL_UART_BATCH;
    words = hal_shared_buffer_uncached(g_uart_tx_words);
    for(size_t i = 0U; i < count; i++)
        words[i] = data[i];

    /* DMA follows UART hardware requests even when the foreground is busy.
       Reuse the staging buffer only after the previous DMA transfer retired. */
    __sync_synchronize();
    g_uart_tx_pending = 1U;
    g_uart_tx_drained_at = 0U;
    dmac_set_single_mode(HAL_EXTERNAL_LINK_DMA, words, (void *)&adapter->THR,
                         DMAC_ADDR_INCREMENT, DMAC_ADDR_NOCHANGE, DMAC_MSIZE_1,
                         DMAC_TRANS_WIDTH_32, count);
    return count;
}

uint8_t hal_external_uart_tx_idle(void)
{
    uint64_t now;

    if(!g_uart_tx_pending)
        return 1U;
    if(!dmac_is_idle(HAL_EXTERNAL_LINK_DMA))
        return 0U;
    now = hal_time_us();
    if(g_uart_tx_drained_at == 0U)
    {
        /* After DMA retires, the FIFO and shifter can still contain nine
           characters. UART FIFO/status readbacks proved unreliable on K210. */
        const uint32_t wire_bits_us = (HAL_EXTERNAL_UART_FIFO_DEPTH + 1U) *
                                      HAL_EXTERNAL_UART_BITS_PER_BYTE * 1000000U;
        g_uart_tx_drained_at = now + (wire_bits_us + g_uart_baud - 1U) / g_uart_baud + 1U;
    }
    if(now < g_uart_tx_drained_at)
        return 0U;
    g_uart_tx_pending = 0U;
    return 1U;
}

void hal_external_i2c_init(uint8_t address, const hal_external_i2c_callbacks_t *callbacks)
{
    volatile i2c_t *adapter = i2c[I2C_DEVICE_0];

    hal_external_i2c_stop();
    hal_external_uart_stop();
    g_i2c_callbacks = callbacks;
    PREPARE_EXTERNAL_I2C();
    sysctl_clock_enable(SYSCTL_CLOCK_I2C0);
    sysctl_clock_set_threshold(SYSCTL_THRESHOLD_I2C0, 3U);
    adapter->enable = 0U;
    adapter->con = I2C_CON_SPEED(1U) | I2C_CON_STOP_DET_IFADDRESSED;
    adapter->sar = I2C_SAR_ADDRESS(address);
    adapter->ss_scl_hcnt = I2C_SS_SCL_HCNT_COUNT(37U);
    adapter->ss_scl_lcnt = I2C_SS_SCL_LCNT_COUNT(40U);
    (void)adapter->clr_intr;
    adapter->dma_rdlr = 0U;
    for(size_t bank = 0U; bank < 2U; ++bank)
    {
        uint32_t *words = hal_shared_buffer_uncached(g_i2c_rx_words[bank]);
        for(size_t index = 0U; index < HAL_EXTERNAL_I2C_DMA_WORDS; ++index)
            words[index] = UINT32_MAX;
    }
    g_i2c_rx_bank = 0U;
    g_i2c_rx_dma_failed = 0U;
    sysctl_dma_select((sysctl_dma_channel_t)HAL_EXTERNAL_LINK_DMA,
                     SYSCTL_DMA_SELECT_I2C0_RX_REQ);
    i2c_target_start_rx(adapter);
    adapter->intr_mask = I2C_INTR_MASK_RX_OVER | I2C_INTR_MASK_START_DET |
                         I2C_INTR_MASK_STOP_DET | I2C_INTR_MASK_RD_REQ;
    plic_set_priority(IRQN_I2C0_INTERRUPT, HAL_EXTERNAL_I2C_IRQ_PRIORITY);
    plic_irq_register(IRQN_I2C0_INTERRUPT, i2c_target_irq, NULL);
    plic_irq_enable(IRQN_I2C0_INTERRUPT);
    adapter->enable = I2C_ENABLE_ENABLE;
    g_i2c_active = 1U;
}

void hal_external_i2c_controller_start(
    uint8_t address, uint32_t frequency_hz,
    const uint8_t *tx, uint32_t tx_size,
    uint8_t *rx, uint32_t rx_size)
{
    volatile i2c_t *adapter;

    hal_external_i2c_stop();
    PREPARE_EXTERNAL_I2C();
    i2c_init(I2C_DEVICE_0, address, 7U, frequency_hz);
    adapter = i2c[I2C_DEVICE_0];
    /* The SDK enables DMA requests, but controller transfers use the IRQ. */
    adapter->dma_cr = 0U;
    (void)adapter->clr_tx_abrt;
    (void)adapter->clr_intr;
    g_i2c_controller_tx = tx;
    g_i2c_controller_rx = rx;
    g_i2c_controller_tx_size = tx_size;
    g_i2c_controller_rx_size = rx_size;
    g_i2c_controller_commands = 0U;
    g_i2c_controller_rx_received = 0U;
    g_i2c_controller_aborted = 0U;
    g_i2c_controller_active = 1U;
    g_i2c_active = 1U;
    adapter->rx_tl = I2C_RX_TL_VALUE(0U);
    adapter->tx_tl = I2C_TX_TL_VALUE(4U);
    plic_irq_disable(IRQN_I2C0_INTERRUPT);
    plic_set_priority(IRQN_I2C0_INTERRUPT,
                      HAL_EXTERNAL_I2C_IRQ_PRIORITY);
    plic_irq_register(IRQN_I2C0_INTERRUPT, i2c_controller_irq, NULL);
    adapter->intr_mask = I2C_INTR_MASK_RX_FULL |
                         I2C_INTR_MASK_RX_OVER |
                         I2C_INTR_MASK_TX_EMPTY |
                         I2C_INTR_MASK_TX_ABRT |
                         I2C_INTR_MASK_STOP_DET;
    i2c_controller_fill_tx(adapter);
    plic_irq_enable(IRQN_I2C0_INTERRUPT);
}

uint8_t hal_external_i2c_controller_aborted(void)
{
    return (uint8_t)(g_i2c_controller_aborted ||
                     i2c[I2C_DEVICE_0]->tx_abrt_source != 0U);
}

uint32_t hal_external_i2c_controller_tx_accepted(void)
{
    uint32_t commands;

    __sync_synchronize();
    commands = g_i2c_controller_commands;
    return commands < g_i2c_controller_tx_size ?
        commands : g_i2c_controller_tx_size;
}

uint32_t hal_external_i2c_controller_rx_received(void)
{
    __sync_synchronize();
    return g_i2c_controller_rx_received;
}

uint8_t hal_external_i2c_controller_idle(void)
{
    volatile i2c_t *adapter = i2c[I2C_DEVICE_0];

    return (uint8_t)(g_i2c_controller_active &&
                     !g_i2c_controller_aborted &&
                     g_i2c_controller_commands ==
                         g_i2c_controller_tx_size +
                         g_i2c_controller_rx_size &&
                     g_i2c_controller_rx_received ==
                         g_i2c_controller_rx_size &&
                     (adapter->status & I2C_STATUS_ACTIVITY) == 0U &&
                     (adapter->status & I2C_STATUS_TFE) != 0U);
}

uint32_t hal_external_i2c_target_lock(void)
{
    plic_irq_disable(IRQN_I2C0_INTERRUPT);
    return g_i2c_active;
}

void hal_external_i2c_target_unlock(uint32_t state)
{
    if(state)
        plic_irq_enable(IRQN_I2C0_INTERRUPT);
}

void hal_external_i2c_stop(void)
{
    volatile i2c_t *adapter;

    if(!g_i2c_active)
        return;
    adapter = i2c[I2C_DEVICE_0];
    adapter->intr_mask = 0U;
    adapter->dma_cr = 0U;
    if(!g_i2c_controller_active)
        (void)external_dma_stop(HAL_EXTERNAL_LINK_DMA, hal_time_us() + HAL_EXTERNAL_DMA_STOP_US);
    adapter->enable = 0U;
    plic_irq_disable(IRQN_I2C0_INTERRUPT);
    plic_irq_unregister(IRQN_I2C0_INTERRUPT);
    g_i2c_callbacks = NULL;
    g_i2c_controller_tx = NULL;
    g_i2c_controller_rx = NULL;
    g_i2c_controller_tx_size = 0U;
    g_i2c_controller_rx_size = 0U;
    g_i2c_controller_commands = 0U;
    g_i2c_controller_rx_received = 0U;
    g_i2c_controller_aborted = 0U;
    g_i2c_controller_active = 0U;
    g_i2c_active = 0U;
}
