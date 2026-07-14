#ifndef HK_SD_H
#define HK_SD_H

#include <stdint.h>


uint8_t sd_init_card(void);
uint8_t sd_read_block(uint32_t lba, uint8_t *dst);
uint8_t sd_write_block(uint32_t lba, const uint8_t *src);
uint8_t sd_write_block_fast(uint32_t lba, const uint8_t *src);

#endif
