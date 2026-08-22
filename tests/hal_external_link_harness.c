#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal_external_link_test_platform.h"
#include "hal_external_link.h"

static uart_t g_uart;
static i2c_t g_i2c;
volatile uart_t *uart[1] = {&g_uart};
volatile i2c_t *i2c[1] = {&g_i2c};
static plic_irq_callback_t g_uart_rx_irq;
static void *g_uart_rx_irq_context;
static uint8_t g_uart_hardware_rx[8];
static size_t g_uart_hardware_rx_size;
static uint8_t g_uart_received[8];
static size_t g_uart_received_size;
static uint32_t g_uart_irq_register_calls;
static uint32_t g_uart_irq_unregister_calls;

static void require_true(uint8_t condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void receive_uart_byte(void *context, uint8_t byte)
{
    (void)context;
    require_true(g_uart_received_size < sizeof(g_uart_received),
                 "UART callback receive capacity exceeded");
    g_uart_received[g_uart_received_size++] = byte;
}

int main(void)
{
    static const uint8_t data[10] = {0U, 1U, 2U, 3U, 4U,
                                      5U, 6U, 7U, 8U, 9U};

    g_uart.TFL = 0U;
    g_uart.THR = 0xffU;
    require_true(hal_external_uart_send_ready(data, sizeof(data)) == 8U,
                 "empty FIFO must accept exactly eight bytes");
    require_true(g_uart.THR == 7U,
                 "empty FIFO must queue the first eight bytes in order");

    g_uart.TFL = 7U;
    g_uart.THR = 0xffU;
    require_true(hal_external_uart_send_ready(data, sizeof(data)) == 1U,
                 "TFL=7 must expose one byte of capacity");
    require_true(g_uart.THR == 0U,
                 "single free slot must queue only the first byte");

    g_uart.TFL = 8U;
    g_uart.THR = 0xa5U;
    require_true(hal_external_uart_send_ready(data, sizeof(data)) == 0U,
                 "TFL=8 must report a full FIFO");
    require_true(g_uart.THR == 0xa5U,
                 "full FIFO must not touch THR");

    g_uart.TFL = 9U;
    require_true(hal_external_uart_send_ready(data, sizeof(data)) == 0U,
                 "out-of-range TFL must fail closed");
    require_true(hal_external_uart_send_ready(NULL, sizeof(data)) == 0U,
                 "null input must be a no-op");

    g_uart.TFL = 0U;
    g_uart.LSR = 1U << 6;
    require_true(hal_external_uart_tx_idle(),
                 "TEMT with an empty FIFO must report drained");
    g_uart.TFL = 1U;
    require_true(!hal_external_uart_tx_idle(),
                 "queued FIFO data must prevent drain completion");
    g_uart.TFL = 0U;
    g_uart.LSR = 0U;
    require_true(!hal_external_uart_tx_idle(),
                 "busy shift register must prevent drain completion");
    memcpy(g_uart_hardware_rx, data, 8U);
    g_uart_hardware_rx_size = 8U;
    hal_external_uart_init(115200U, receive_uart_byte, NULL);
    require_true(g_uart_irq_register_calls == 1U && g_uart_rx_irq != NULL,
                 "UART init must register one RX interrupt");
    memcpy(g_uart_hardware_rx, data, 8U);
    g_uart_hardware_rx_size = 8U;
    require_true(g_uart_rx_irq(g_uart_rx_irq_context) == 0,
                 "UART RX interrupt must acknowledge input");
    require_true(g_uart_hardware_rx_size == 0U &&
                 g_uart_received_size == 8U &&
                 memcmp(g_uart_received, data, 8U) == 0,
                 "UART RX interrupt must preserve all FIFO bytes");
    hal_external_uart_stop();
    require_true(g_uart_irq_unregister_calls == 1U && g_uart_rx_irq == NULL,
                 "UART stop must unregister its RX interrupt");
    puts("HAL_EXTERNAL_LINK_OK boundaries=4 depth=8 idle=3 uart_irq=8");
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

int uart_receive_data(int device, char *data, size_t length)
{
    size_t count;

    (void)device;
    count = length < g_uart_hardware_rx_size ?
        length : g_uart_hardware_rx_size;
    if(count != 0U)
    {
        memcpy(data, g_uart_hardware_rx, count);
        memmove(g_uart_hardware_rx, g_uart_hardware_rx + count,
                g_uart_hardware_rx_size - count);
        g_uart_hardware_rx_size -= count;
    }
    return (int)count;
}

int uart_send_data(int device, const char *data, size_t length)
{
    (void)device; (void)data;
    return (int)length;
}

void uart_irq_register(int device, int mode,
                       plic_irq_callback_t callback, void *context,
                       uint32_t priority)
{
    require_true(device == UART_DEVICE_1 && mode == UART_RECEIVE &&
                 priority == 2U, "UART RX interrupt registration mismatch");
    g_uart_rx_irq = callback;
    g_uart_rx_irq_context = context;
    g_uart_irq_register_calls++;
}

void uart_irq_unregister(int device, int mode)
{
    require_true(device == UART_DEVICE_1 && mode == UART_RECEIVE,
                 "UART RX interrupt unregister mismatch");
    g_uart_rx_irq = NULL;
    g_uart_rx_irq_context = NULL;
    g_uart_irq_unregister_calls++;
}

void i2c_init_as_slave(int device, uint32_t address, uint32_t address_width,
                       const i2c_slave_handler_t *handler)
{
    (void)device; (void)address; (void)address_width; (void)handler;
}

void i2c_init(int device, uint32_t address, uint32_t address_width,
              uint32_t frequency)
{
    (void)device; (void)address; (void)address_width; (void)frequency;
}

int plic_irq_disable(int interrupt)
{
    (void)interrupt;
    return 0;
}

int plic_irq_enable(int interrupt)
{
    (void)interrupt;
    return 0;
}
