#include "debug_console_service.h"

#include <string.h>

#include "../hal/hal_uart.h"

size_t debug_console_read(uint8_t *data, size_t len)
{
    return hal_debug_uart_receive(data, len);
}

void debug_console_write(const uint8_t *data, size_t len)
{
    if(len)
        hal_debug_uart_send(data, len);
}

void debug_console_write_text(const char *text)
{
    debug_console_write((const uint8_t *)text, strlen(text));
}
