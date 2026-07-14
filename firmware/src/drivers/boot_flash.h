#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stddef.h>
#include <stdint.h>

void boot_flash_init(uint32_t hz);
void boot_flash_read_id(uint8_t id[3]);
void boot_flash_read(uint32_t addr, uint8_t *data, size_t len);
void boot_flash_sector_erase(uint32_t addr);
void boot_flash_page_program(uint32_t addr, const uint8_t *data, size_t len);

#endif
