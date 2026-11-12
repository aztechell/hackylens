#ifndef HK_SCRIPT_SD_STORE_H
#define HK_SCRIPT_SD_STORE_H

#include <stddef.h>
#include <stdint.h>
#include "../storage/sd_card.h"

#define SCRIPT_SD_NAME_MAX 12U

typedef enum
{
    SCRIPT_SD_OK,
    SCRIPT_SD_PENDING,
    SCRIPT_SD_INVALID_NAME,
    SCRIPT_SD_EXISTS,
    SCRIPT_SD_NO_MEDIA,
    SCRIPT_SD_FULL,
    SCRIPT_SD_BUSY,
    SCRIPT_SD_IO,
} script_sd_result_t;

/* One core-0 writer scoped to /PYDATA. PENDING operations resume on the
 * next call. A successful publish replaces only its own temporary entry. */
script_sd_result_t script_sd_begin(const char *name, size_t length, uint8_t bmp_only);
script_sd_result_t script_sd_write(const uint8_t *data, size_t length, size_t *accepted);
script_sd_result_t script_sd_write_run(uint32_t remaining, sd_block_source_t source,
                                       void *context, size_t *accepted);
script_sd_result_t script_sd_finish(void);
void script_sd_abort(void);

#endif
