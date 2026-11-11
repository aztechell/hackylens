#ifndef HK_ACCELEROMETER_PROVIDER_H
#define HK_ACCELEROMETER_PROVIDER_H

#include <hackylens/capability/accelerometer.h>

struct hk_accelerometer
{
    hk_result_t (*read)(hk_acceleration_t *sample);
    hk_result_t (*read_motion)(hk_acceleration_motion_t *motion);
};

extern const hk_accelerometer_t hk_accelerometer_binding;

#endif
