#include "data_sdk_private.h"

#include "app_services.h"

static uint8_t s_owner;

hk_result_t app_data_result(script_sd_result_t status)
{
    switch(status)
    {
    case SCRIPT_SD_OK: return HK_OK;
    case SCRIPT_SD_PENDING: return HK_PENDING;
    case SCRIPT_SD_INVALID_NAME: return HK_ERR_INVALID_ARGUMENT;
    case SCRIPT_SD_EXISTS: return HK_APP_DATA_ERR_EXISTS;
    case SCRIPT_SD_NO_MEDIA: return HK_APP_DATA_ERR_NO_MEDIA;
    case SCRIPT_SD_FULL: return HK_ERR_LIMIT;
    case SCRIPT_SD_BUSY: return HK_ERR_BUSY;
    case SCRIPT_SD_STALE_FRAME: return HK_ERR_STALE_HANDLE;
    default: return HK_ERR_IO;
    }
}

void app_data_retire(void)
{
    script_sd_abort(&s_owner);
}

hk_result_t hk_app_data_begin(const hk_app_context_t *ctx, const char *name, size_t length)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ? app_data_result(script_sd_begin(&s_owner, name, length, 0U)) : result;
}

hk_result_t hk_app_data_write(const hk_app_context_t *ctx, const uint8_t *data,
                              size_t length, size_t *accepted)
{
    if(!accepted)
        return HK_ERR_INVALID_ARGUMENT;
    *accepted = 0U;
    if((!data && length) || length > HK_APP_DATA_CHUNK_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    hk_result_t result = app_service_context(ctx, 1U);
    if(result != HK_OK)
        return result;
    result = app_data_result(script_sd_write(&s_owner, data, length, accepted));
    if(result < 0)
        *accepted = 0U;
    return result;
}

hk_result_t hk_app_data_finish(const hk_app_context_t *ctx)
{
    hk_result_t result = app_service_context(ctx, 1U);
    return result == HK_OK ? app_data_result(script_sd_finish(&s_owner)) : result;
}

hk_result_t hk_app_data_cancel(const hk_app_context_t *ctx)
{
    hk_result_t result = app_service_context(ctx, 0U);
    if(result == HK_OK)
        app_data_retire();
    return result;
}
