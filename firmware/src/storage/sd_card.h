#ifndef HK_STORAGE_SD_CARD_H
#define HK_STORAGE_SD_CARD_H

#include <stdint.h>

/* Permanent private storage boundary for bounded SD block operations. */
uint8_t sd_card_init(void);
uint8_t sd_card_read_block(uint32_t lba, uint8_t *dst);
uint8_t sd_card_write_block(uint32_t lba, const uint8_t *src);
uint8_t sd_card_write_block_fast(uint32_t lba, const uint8_t *src);

/* The source returns a complete sector and performs no SD I/O. It can be
 * called again for the same sector if the card needs single-block writes. */
typedef const uint8_t *(*sd_block_source_t)(void *context, uint16_t index);
uint8_t sd_card_write_run(uint32_t lba, uint16_t count,
                          sd_block_source_t source, void *context);

#endif
