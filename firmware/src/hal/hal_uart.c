#include "hal_uart.h"

#include <uart.h>

uint32_t hal_debug_uart_receive(uint8_t *data, size_t len)
{
    return uart_receive_data(UART_DEVICE_3, (char *)data, len);
}

void hal_debug_uart_send(const uint8_t *data, size_t len)
{
    if(len)
        uart_send_data(UART_DEVICE_3, (const char *)data, len);
}
