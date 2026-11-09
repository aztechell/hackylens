#include <hackylens/app/accelerometer.h>

#include <stddef.h>

hk_result_t hk_app_context_accelerometer(const hk_app_context_t *ctx,
                                         const hk_accelerometer_t **accelerometer)
{
    if(!accelerometer)
        return HK_ERR_INVALID_ARGUMENT;
    *accelerometer = NULL;
    const char *id;
    uint32_t generation;
    hk_result_t result = hk_app_context_identity(ctx, &id, &generation);
    if(result != HK_OK)
        return result;
    *accelerometer = hk_accelerometer_service();
    return *accelerometer ? HK_OK : HK_ERR_CAPABILITY_ABSENT;
}
