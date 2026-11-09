#include "accelerometer_provider.h"

#include <stddef.h>

const hk_accelerometer_t *hk_accelerometer_service(void)
{
    return hk_accelerometer_binding.read ? &hk_accelerometer_binding : NULL;
}

hk_result_t hk_accelerometer_read(const hk_accelerometer_t *accelerometer,
                                 hk_acceleration_t *sample)
{
    if(!sample)
        return HK_ERR_INVALID_ARGUMENT;
    return accelerometer && accelerometer->read ? accelerometer->read(sample) :
        HK_ERR_CAPABILITY_ABSENT;
}
