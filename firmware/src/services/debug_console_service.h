#ifndef DEBUG_CONSOLE_SERVICE_H
#define DEBUG_CONSOLE_SERVICE_H

#include <stddef.h>
#include <stdint.h>

size_t debug_console_read(uint8_t *data, size_t len);
void debug_console_write(const uint8_t *data, size_t len);
void debug_console_write_text(const char *text);

#endif
