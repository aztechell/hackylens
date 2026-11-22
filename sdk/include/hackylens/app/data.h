#ifndef HACKYLENS_APP_DATA_H
#define HACKYLENS_APP_DATA_H

#include <stddef.h>
#include <hackylens/app/context.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HK_APP_DATA_NAME_MAX 12U
#define HK_APP_DATA_CHUNK_MAX 256U

enum
{
    HK_APP_DATA_ERR_EXISTS = -1000,
    HK_APP_DATA_ERR_NO_MEDIA = -1001,
};

/* Declare sd-card in app.toml. Files are created under /PYDATA with an
 * ASCII FAT 8.3 name. TMP is reserved. Existing files are preserved.
 * Repeat begin with the same name until HK_OK or an error. Advance by
 * accepted bytes on HK_OK or HK_PENDING, then call finish until HK_OK
 * publishes the file. The buffer is borrowed only for the write call.
 * Cancel or leave the app to discard an unfinished file. */
hk_result_t hk_app_data_begin(const hk_app_context_t *ctx, const char *name, size_t length);
hk_result_t hk_app_data_write(const hk_app_context_t *ctx, const uint8_t *data,
                              size_t length, size_t *accepted);
hk_result_t hk_app_data_finish(const hk_app_context_t *ctx);
hk_result_t hk_app_data_cancel(const hk_app_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
