#ifndef HK_APP_SERVICES_H
#define HK_APP_SERVICES_H

#include <hackylens/app/context.h>

/* The SDK borrows the foreground runtime's authority only in a callback. */
static inline hk_result_t app_service_context(const hk_app_context_t *ctx,
                                              uint8_t starts_work)
{
    const char *id;
    uint32_t generation;
    hk_deadline_t deadline;
    hk_result_t result = hk_app_context_identity(ctx, &id, &generation);
    if(result == HK_OK && starts_work &&
       hk_app_context_teardown_deadline(ctx, &deadline) == HK_OK)
        return HK_ERR_INVALID_STATE;
    return result;
}

void app_camera_retire(void);
void app_data_retire(void);
hk_result_t app_audio_retire(void);

#endif
