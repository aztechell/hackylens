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
    SCRIPT_SD_STALE_FRAME,
} script_sd_result_t;

/* One core-0 writer scoped to /PYDATA. Repeat a pending begin with the same
 * owner and name. A successful publish replaces only its own temporary entry. */
script_sd_result_t script_sd_begin(const void *owner, const char *name, size_t length,
                                   uint8_t bmp_only);
script_sd_result_t script_sd_write(const void *owner, const uint8_t *data, size_t length,
                                   size_t *accepted);
script_sd_result_t script_sd_write_run(const void *owner, uint32_t remaining,
                                       sd_block_source_t source, void *context,
                                       size_t *accepted);
script_sd_result_t script_sd_finish(const void *owner);
void script_sd_abort(const void *owner);

#endif
