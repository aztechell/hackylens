#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal_external_link_test_platform.h"
#include "hal_external_link.h"
#include "hal_shared_buffer.h"
#include "hal_time.h"

static uart_t g_uart;
static i2c_t g_i2c;
static dmac_t g_dmac;
volatile dmac_t *dmac = &g_dmac;
static const uint32_t *g_dma_words;
static uint32_t *g_dma_rx_words;
static uint32_t g_dma_held_word = UINT32_MAX;
static size_t g_dma_count;
static uint8_t g_dma_busy;
static int g_dma_request = -1;
static uint8_t g_dma_aborted;
volatile uart_t *uart[1] = {&g_uart};
volatile i2c_t *i2c[1] = {&g_i2c};
static uint8_t g_uart_rx_busy;
static uint8_t g_uart_rx_selected;
static uint32_t *g_uart_rx_dma_words;
static uint32_t g_uart_held_word = UINT32_MAX;
static uint8_t g_uart_received[8];
static size_t g_uart_received_size;
static plic_irq_callback_t g_i2c_irq;
static void *g_i2c_irq_context;
static uint8_t g_i2c_irq_enabled;
static uint32_t g_i2c_irq_register_calls;
static uint32_t g_i2c_irq_unregister_calls;
static uint32_t g_i2c_fifo[8];
static size_t g_i2c_fifo_size;
static uint8_t g_i2c_hardware_rx[8];
static size_t g_i2c_hardware_rx_size;
static uint32_t g_i2c_commands[64];
static size_t g_i2c_command_count;
static size_t g_i2c_expected_commands;
static uint8_t g_i2c_source[32];
static size_t g_i2c_source_size;
static size_t g_i2c_source_position;
static uint8_t g_i2c_fifo_gap;
static uint8_t g_i2c_target_data[12];
static size_t g_i2c_target_size;
static size_t g_i2c_target_size_at_stop;
static size_t g_i2c_target_overflows;
static uint64_t g_now_us;

uint64_t hal_time_us(void)
{
    return g_now_us;
}

static void require_true(uint8_t condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

void *hal_shared_buffer_uncached(void *pointer)
{
    return pointer;
}

void sysctl_dma_select(sysctl_dma_channel_t channel, int source)
{
    if(channel == DMAC_CHANNEL2)
    {
        require_true(source == SYSCTL_DMA_SELECT_UART1_RX_REQ,
                     "UART receiver must select its own DMA request");
        g_uart_rx_selected = 1U;
        return;
    }
    require_true(channel == DMAC_CHANNEL1 &&
                     (source == SYSCTL_DMA_SELECT_UART1_TX_REQ ||
                      source == SYSCTL_DMA_SELECT_I2C0_RX_REQ),
                 "external link must select DMA1 with the correct request");
    g_dma_request = source;
}

int dmac_is_idle(int channel)
{
    require_true(channel == DMAC_CHANNEL1 || channel == DMAC_CHANNEL2,
                 "external link DMA channels are reserved");
    return channel == DMAC_CHANNEL2 ? !g_uart_rx_busy : !g_dma_busy;
}

void dmac_channel_disable(int channel)
{
    uint64_t bit = UINT64_C(1) << channel;
    require_true(g_dmac.chen == ((bit << 32U) | (bit << 40U)),
                 "stopping DMA must abort only the selected channel");
    if(channel == DMAC_CHANNEL2)
    {
        if(g_uart_held_word != UINT32_MAX)
        {
            g_uart_rx_dma_words[7] = g_uart_held_word;
            g_uart_held_word = UINT32_MAX;
        }
        g_uart_rx_busy = 0U;
        return;
    }
    g_dma_aborted = 1U;
    if(g_dma_request == SYSCTL_DMA_SELECT_I2C0_RX_REQ &&
       g_dma_held_word != UINT32_MAX)
    {
        g_dma_rx_words[0] = g_dma_held_word;
        g_dma_held_word = UINT32_MAX;
    }
    g_dma_busy = 0U;
}

void dmac_set_single_mode(int channel, const void *source, void *dest,
                          int source_increment, int dest_increment,
                          int burst_size, int width, size_t count)
{
    require_true(dmac_is_idle(channel) &&
                     burst_size == DMAC_MSIZE_1 && width == DMAC_TRANS_WIDTH_32,
                 "DMA must obey the hardware handshake and word width");
    if(channel == DMAC_CHANNEL2)
    {
        require_true(g_uart_rx_selected && source == (const void *)&g_uart.RBR &&
                         source_increment == DMAC_ADDR_NOCHANGE &&
                         dest_increment == DMAC_ADDR_INCREMENT && count == 257U,
                     "UART RX DMA must capture one bounded receive window");
        g_uart_rx_dma_words = dest;
        g_uart_rx_busy = 1U;
        return;
    }
    if(g_dma_request == SYSCTL_DMA_SELECT_UART1_TX_REQ)
    {
        require_true(dest == (void *)&g_uart.THR &&
                         source_increment == DMAC_ADDR_INCREMENT &&
                         dest_increment == DMAC_ADDR_NOCHANGE,
                     "UART DMA must write staged words to THR");
        g_dma_words = source;
    }
    else
    {
        require_true(g_dma_request == SYSCTL_DMA_SELECT_I2C0_RX_REQ &&
                         source == (const void *)&g_i2c.data_cmd &&
                         source_increment == DMAC_ADDR_NOCHANGE &&
                         dest_increment == DMAC_ADDR_INCREMENT,
                     "I2C DMA must drain the target RX FIFO into SRAM");
        g_dma_rx_words = dest;
    }
    g_dma_count = count;
    g_dma_busy = 1U;
}

static void receive_uart_byte(void *context, uint8_t byte)
{
    (void)context;
    require_true(g_uart_received_size < sizeof(g_uart_received),
                 "UART callback receive capacity exceeded");
    g_uart_received[g_uart_received_size++] = byte;
}

static void receive_i2c_target_byte(uint8_t byte)
{
    require_true(g_i2c_target_size < sizeof(g_i2c_target_data),
                 "I2C target must not forward a duplicate SDK receive");
    g_i2c_target_data[g_i2c_target_size++] = byte;
}

static void receive_i2c_target_event(hal_external_i2c_event_t event)
{
    if(event == HAL_EXTERNAL_I2C_EVENT_STOP)
        g_i2c_target_size_at_stop = g_i2c_target_size;
    else if(event == HAL_EXTERNAL_I2C_EVENT_OVERFLOW)
        ++g_i2c_target_overflows;
}

static uint8_t send_i2c_target_byte(void)
{
    return 0xa5U;
}

static void reset_i2c_model(const uint8_t *source, size_t source_size,
                            size_t expected_commands)
{
    memset(&g_i2c, 0, sizeof(g_i2c));
    memset(g_i2c_fifo, 0, sizeof(g_i2c_fifo));
    memset(g_i2c_hardware_rx, 0, sizeof(g_i2c_hardware_rx));
    memset(g_i2c_commands, 0, sizeof(g_i2c_commands));
    memset(g_i2c_source, 0, sizeof(g_i2c_source));
    if(source_size > sizeof(g_i2c_source))
        source_size = sizeof(g_i2c_source);
    if(source_size != 0U)
        memcpy(g_i2c_source, source, source_size);
    g_i2c_fifo_size = 0U;
    g_i2c_hardware_rx_size = 0U;
    g_i2c_command_count = 0U;
    g_i2c_expected_commands = expected_commands;
    g_i2c_source_size = source_size;
    g_i2c_source_position = 0U;
    g_i2c_fifo_gap = 0U;
}

static void invoke_i2c_irq(uint32_t status)
{
    if(!g_i2c_irq_enabled || !g_i2c_irq)
        return;
    g_i2c.intr_stat = status & g_i2c.intr_mask;
    if(g_i2c.intr_stat != 0U)
        require_true(g_i2c_irq(g_i2c_irq_context) == 0,
                     "I2C interrupt must acknowledge input");
    g_i2c.intr_stat = 0U;
}

static void drain_i2c_command(void)
{
    uint32_t command;
    uint32_t irq_status = 0U;

    require_true(g_i2c_fifo_size != 0U,
                 "I2C hardware cannot drain an empty FIFO");
    command = g_i2c_fifo[0];
    memmove(g_i2c_fifo, g_i2c_fifo + 1,
            (g_i2c_fifo_size - 1U) * sizeof(g_i2c_fifo[0]));
    g_i2c_fifo_size--;
    g_i2c.txflr = (uint32_t)g_i2c_fifo_size;
    if((command & I2C_DATA_CMD_CMD) != 0U)
    {
        uint8_t value = g_i2c_source_position < g_i2c_source_size ?
            g_i2c_source[g_i2c_source_position] :
            (uint8_t)(0x80U + g_i2c_source_position);

        require_true(g_i2c_hardware_rx_size <
                         sizeof(g_i2c_hardware_rx),
                     "I2C RX FIFO overflowed in the model");
        g_i2c_source_position++;
        g_i2c_hardware_rx[g_i2c_hardware_rx_size++] = value;
        g_i2c.rxflr = (uint32_t)g_i2c_hardware_rx_size;
        irq_status |= I2C_INTR_STAT_RX_FULL;
    }
    if(g_i2c_fifo_size <= g_i2c.tx_tl)
        irq_status |= I2C_INTR_STAT_TX_EMPTY;
    if(g_i2c_fifo_size == 0U &&
       g_i2c_command_count < g_i2c_expected_commands)
        g_i2c_fifo_gap = 1U;
    invoke_i2c_irq(irq_status);
    if(g_i2c_fifo_size == 0U &&
       g_i2c_command_count == g_i2c_expected_commands)
    {
        g_i2c.status = I2C_STATUS_TFE;
        invoke_i2c_irq(I2C_INTR_STAT_STOP_DET |
                       (g_i2c_hardware_rx_size != 0U ?
                            I2C_INTR_STAT_RX_FULL : 0U));
    }
}

static void run_i2c_transaction(const uint8_t *tx, size_t tx_size,
                                const uint8_t *source, size_t rx_size)
{
    uint8_t rx[32];
    size_t total = tx_size + rx_size;

    require_true(rx_size <= sizeof(rx), "I2C test RX capacity exceeded");
    memset(rx, 0xa5, sizeof(rx));
    reset_i2c_model(source, rx_size, total);
    hal_external_i2c_controller_start(
        0x42U, 100000U, tx, (uint32_t)tx_size,
        rx, (uint32_t)rx_size);
    require_true(g_i2c.dma_cr == 0U,
                 "controller must use IRQs without leaving DMA requests pending");
    require_true(g_i2c_command_count ==
                     (total < 8U ? total : 8U),
                 "controller must prime exactly one FIFO window");
    while(g_i2c_fifo_size != 0U)
        drain_i2c_command();
    require_true(g_i2c_command_count == total,
                 "all I2C commands must be issued");
    require_true(!g_i2c_fifo_gap,
                 "controller must refill before the FIFO becomes empty");
    require_true(hal_external_i2c_controller_tx_accepted() == tx_size,
                 "controller TX acceptance mismatch");
    require_true(hal_external_i2c_controller_rx_received() == rx_size,
                 "controller RX completion mismatch");
    require_true(rx_size == 0U || memcmp(rx, source, rx_size) == 0,
                 "controller RX payload mismatch");
    require_true(hal_external_i2c_controller_idle(),
                 "drained controller transaction must become idle");
    for(size_t index = 0U; index < tx_size; ++index)
        require_true(g_i2c_commands[index] == tx[index],
                     "write prefix changed in the command stream");
    for(size_t index = tx_size; index < total; ++index)
        require_true(g_i2c_commands[index] == I2C_DATA_CMD_CMD,
                     "read command ordering changed");
    hal_external_i2c_stop();
}

int main(void)
{
    static const uint8_t data[10] = {0U, 1U, 2U, 3U, 4U,
                                      5U, 6U, 7U, 8U, 9U};
    static const struct
    {
        uint32_t baud;
        uint32_t drain_us;
    } faster_uart_rates[] = {{115200U, 783U}, {1000000U, 91U}};

    hal_external_uart_init(9600U, receive_uart_byte, NULL);
    require_true(g_uart_rx_busy && g_uart_rx_dma_words[0] == UINT32_MAX,
                 "UART init must arm the receive bank");

    /* FIFO readbacks can report empty while the hardware is active. */
    g_uart.TFL = 0U;
    g_uart.LSR = 1U << 6;
    g_now_us = 1000U;
    require_true(hal_external_uart_send_ready(data, 8U) == 8U &&
                     g_dma_count == 8U && g_dma_words[0] == 0U &&
                     g_dma_words[7] == 7U,
                 "DMA must stage each byte as a UART word in order");
    g_now_us += 10000U;
    require_true(hal_external_uart_send_ready(data + 8U, 2U) == 0U &&
                     g_dma_words[0] == 0U && !hal_external_uart_tx_idle(),
                 "busy DMA must keep its buffer regardless of elapsed time");
    g_dma_busy = 0U;
    require_true(hal_external_uart_send_ready(data + 8U, 2U) == 2U &&
                     g_dma_count == 2U && g_dma_words[0] == 8U &&
                     g_dma_words[1] == 9U,
                 "the next transfer uses hardware flow control without a software gap");
    g_dma_busy = 0U;
    require_true(!hal_external_uart_tx_idle(),
                 "DMA completion does not mean the last stop bit left the wire");
    g_now_us += 9375U;
    require_true(!hal_external_uart_tx_idle(),
                 "nine characters at 9600 baud must not complete early");
    ++g_now_us;
    require_true(hal_external_uart_tx_idle(),
                 "the transmit FIFO and shifter must drain after 9376 us");
    require_true(hal_external_uart_send_ready(NULL, sizeof(data)) == 0U,
                 "null input must be a no-op");
    uint32_t *old_rx_bank = g_uart_rx_dma_words;
    for(size_t i = 0U; i < 7U; ++i)
        old_rx_bank[i] = data[i];
    g_uart_held_word = data[7];
    require_true(hal_external_uart_collect_rx() && g_uart_rx_busy &&
                     g_uart_rx_dma_words != old_rx_bank &&
                     g_uart_received_size == 8U &&
                     memcmp(g_uart_received, data, 8U) == 0,
                 "UART must rotate its receive bank and deliver partial DMA bursts");
    g_uart_rx_dma_words[256] = 0x7aU;
    require_true(!hal_external_uart_collect_rx() && g_uart_received_size == 8U,
                 "overflow must not publish an incomplete UART frame");
    hal_external_uart_stop();
    require_true(!g_uart_rx_busy && !hal_external_uart_collect_rx(),
                 "UART stop must release the RX DMA channel");

    for(size_t i = 0U; i < sizeof(faster_uart_rates) / sizeof(faster_uart_rates[0]); ++i)
    {
        hal_external_uart_init(faster_uart_rates[i].baud, receive_uart_byte, NULL);
        uint8_t batch[40];
        memset(batch, 0xa5, sizeof(batch));
        require_true(hal_external_uart_send_ready(batch, sizeof(batch)) == 32U &&
                         g_dma_count == 32U && g_dma_words[31] == 0xa5U,
                     "one foreground poll must stage more than a single FIFO window");
        g_dma_busy = 0U;
        require_true(!hal_external_uart_tx_idle(),
                     "drain timing begins after DMA completes");
        g_now_us += faster_uart_rates[i].drain_us - 1U;
        require_true(!hal_external_uart_tx_idle(),
                     "faster baud rates still wait for the last stop bit");
        ++g_now_us;
        require_true(hal_external_uart_tx_idle() &&
                         hal_external_uart_send_ready(data, 8U) == 8U,
                     "a new transfer can start after the previous one completed");
        g_dma_aborted = 0U;
        hal_external_uart_stop();
        require_true(g_dma_aborted && hal_external_uart_tx_idle() &&
                         hal_external_uart_send_ready(data, 1U) == 0U,
                     "stop must abort DMA, release its staging buffer and disable writes");
    }

    {
        static const uint8_t prefix[1] = {0x10U};
        static const uint8_t response[8] = {
            'H', 'K', '2', 'I', '2', 'C', '4', '2',
        };

        run_i2c_transaction(prefix, sizeof(prefix),
                            response, sizeof(response));
    }
    {
        uint8_t prefix[20];
        uint8_t response[20];

        for(size_t index = 0U; index < sizeof(prefix); ++index)
        {
            prefix[index] = (uint8_t)(index + 1U);
            response[index] = (uint8_t)(0xc0U + index);
        }
        run_i2c_transaction(prefix, sizeof(prefix),
                            response, sizeof(response));
    }
    {
        static const uint8_t prefix[1] = {0x20U};
        uint8_t rx[16];
        uint8_t before[16];

        memset(rx, 0x5a, sizeof(rx));
        reset_i2c_model(NULL, 0U, sizeof(prefix) + sizeof(rx));
        hal_external_i2c_controller_start(
            0x42U, 100000U, prefix, sizeof(prefix), rx, sizeof(rx));
        drain_i2c_command();
        memcpy(before, rx, sizeof(rx));
        hal_external_i2c_stop();
        invoke_i2c_irq(I2C_INTR_STAT_TX_EMPTY |
                       I2C_INTR_STAT_RX_FULL);
        require_true(memcmp(rx, before, sizeof(rx)) == 0,
                     "stopped controller must not mutate borrowed RX");
    }
    {
        static const uint8_t prefix[1] = {0x30U};

        reset_i2c_model(NULL, 0U, sizeof(prefix));
        hal_external_i2c_controller_start(
            0x41U, 100000U, prefix, sizeof(prefix), NULL, 0U);
        g_i2c.tx_abrt_source = 1U;
        invoke_i2c_irq(I2C_INTR_STAT_TX_ABRT);
        require_true(hal_external_i2c_controller_aborted(),
                     "NACK abort must remain latched");
        hal_external_i2c_stop();
    }
    require_true(g_i2c_irq_register_calls == 4U &&
                 g_i2c_irq_unregister_calls == 4U,
                 "each controller transaction must own one IRQ lifecycle");
    {
        static const uint8_t input[] = "HK123456789!";
        static const hal_external_i2c_callbacks_t callbacks = {
            .receive = receive_i2c_target_byte,
            .transmit = send_i2c_target_byte,
            .event = receive_i2c_target_event,
        };

        reset_i2c_model(NULL, 0U, 0U);
        hal_external_i2c_init(0x32U, &callbacks);
        require_true(g_dma_busy && g_dma_count >= 257U && g_i2c.sar == 0x32U &&
                         g_i2c.dma_cr == I2C_DMA_CR_RDMAE,
                     "target must arm hardware RX DMA before accepting writes");
        uint32_t *first = g_dma_rx_words;
        for(size_t i = 0U; i < sizeof(input) - 1U; ++i)
            first[i] = input[i];
        require_true(g_i2c_target_size == 0U,
                     "bytes may arrive in the DMA buffer before the next interrupt");
        invoke_i2c_irq(I2C_INTR_STAT_STOP_DET);
        require_true(g_i2c_target_size_at_stop == sizeof(g_i2c_target_data) &&
                         memcmp(g_i2c_target_data, input, g_i2c_target_size) == 0,
                     "the target must deliver all DMA bytes before STOP");
        require_true(g_dma_busy && g_dma_rx_words != first && first[0] == UINT32_MAX,
                     "target must rearm the other buffer and recycle the previous one");
        invoke_i2c_irq(I2C_INTR_STAT_RD_REQ | I2C_INTR_STAT_STOP_DET);
        require_true(g_i2c.data_cmd == 0xa5U && g_dma_busy,
                     "read events must leave RX DMA ready for the next write");
        g_dma_rx_words[0] = 0xeeU;
        invoke_i2c_irq(I2C_INTR_STAT_RX_OVER | I2C_INTR_STAT_STOP_DET);
        require_true(g_i2c_target_overflows == 1U &&
                         g_i2c_target_size == sizeof(g_i2c_target_data),
                     "hardware RX overflow must discard a partial write");
        g_i2c_target_size = 0U;
        g_dma_held_word = 0x42U;
        invoke_i2c_irq(I2C_INTR_STAT_STOP_DET);
        require_true(g_i2c_target_size_at_stop == 1U && g_i2c_target_data[0] == 0x42U,
                     "short writes held inside DMA must be flushed and reported at STOP");
        hal_external_i2c_stop();
    }
    puts("HAL_EXTERNAL_LINK_OK uart_rates=3 depth=8 uart_dma_rx=8 i2c_continuous=2 i2c_stop=1 i2c_abort=1 i2c_target_fifo=1");
    return 0;
}

void board_external_link_uart_pins(void) {}
void board_external_link_i2c_pins(void) {}
void uart_init(int device) { (void)device; }

void uart_configure(int device, uint32_t baud, int width,
                    int stop, int parity)
{
    (void)device; (void)baud; (void)width; (void)stop; (void)parity;
}

void sysctl_clock_enable(int clock)
{
    require_true(clock == SYSCTL_CLOCK_I2C0, "target must clock I2C0");
}

void sysctl_clock_set_threshold(int clock, uint32_t divisor)
{
    require_true(clock == SYSCTL_THRESHOLD_I2C0 && divisor == 3U,
                 "target must configure the I2C0 divider");
}

void i2c_init(int device, uint32_t address, uint32_t address_width,
              uint32_t frequency)
{
    (void)device; (void)address; (void)address_width; (void)frequency;
    g_i2c.status = I2C_STATUS_ACTIVITY;
    g_i2c.dma_cr = 3U;
}

int plic_irq_disable(int interrupt)
{
    require_true(interrupt == IRQN_I2C0_INTERRUPT,
                 "unexpected disabled IRQ");
    g_i2c_irq_enabled = 0U;
    return 0;
}

int plic_irq_enable(int interrupt)
{
    require_true(interrupt == IRQN_I2C0_INTERRUPT,
                 "unexpected enabled IRQ");
    g_i2c_irq_enabled = 1U;
    return 0;
}

int plic_set_priority(int interrupt, uint32_t priority)
{
    require_true(interrupt == IRQN_I2C0_INTERRUPT && priority == 4U,
                 "I2C IRQ priority mismatch");
    return 0;
}

void plic_irq_register(int interrupt,
                       plic_irq_callback_t callback, void *context)
{
    require_true(interrupt == IRQN_I2C0_INTERRUPT && callback != NULL,
                 "I2C IRQ registration mismatch");
    g_i2c_irq = callback;
    g_i2c_irq_context = context;
    g_i2c_irq_register_calls++;
}

void plic_irq_unregister(int interrupt)
{
    require_true(interrupt == IRQN_I2C0_INTERRUPT,
                 "I2C IRQ unregister mismatch");
    g_i2c_irq = NULL;
    g_i2c_irq_context = NULL;
    g_i2c_irq_unregister_calls++;
}

void hal_external_i2c_test_write_command(uint32_t command)
{
    require_true(g_i2c_fifo_size < sizeof(g_i2c_fifo) /
                                      sizeof(g_i2c_fifo[0]),
                 "controller wrote past the eight-entry FIFO");
    require_true(g_i2c_command_count < sizeof(g_i2c_commands) /
                                          sizeof(g_i2c_commands[0]),
                 "I2C command log overflowed");
    g_i2c_fifo[g_i2c_fifo_size++] = command;
    g_i2c_commands[g_i2c_command_count++] = command;
    g_i2c.txflr = (uint32_t)g_i2c_fifo_size;
}

uint32_t hal_external_i2c_test_read_data(void)
{
    uint8_t value;

    require_true(g_i2c_hardware_rx_size != 0U,
                 "controller read an empty RX FIFO");
    value = g_i2c_hardware_rx[0];
    memmove(g_i2c_hardware_rx, g_i2c_hardware_rx + 1,
            g_i2c_hardware_rx_size - 1U);
    g_i2c_hardware_rx_size--;
    g_i2c.rxflr = (uint32_t)g_i2c_hardware_rx_size;
    return value;
}
