#include <stdio.h>
#include <stdlib.h>

#include "hal_external_link_test_platform.h"
#include "hal_external_link.h"

static uart_t g_uart;
static i2c_t g_i2c;
volatile uart_t *uart[1] = {&g_uart};
volatile i2c_t *i2c[1] = {&g_i2c};

static void require_true(uint8_t condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
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
    puts("HAL_EXTERNAL_LINK_OK boundaries=4 depth=8 idle=3");
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
    (void)device; (void)data; (void)length;
    return 0;
}

int uart_send_data(int device, const char *data, size_t length)
{
    (void)device; (void)data;
    return (int)length;
}

void i2c_init_as_slave(int device, uint32_t address, uint32_t address_width,
                       const i2c_slave_handler_t *handler)
{
    (void)device; (void)address; (void)address_width; (void)handler;
}

int plic_irq_disable(int interrupt)
{
    (void)interrupt;
    return 0;
}
