#ifndef HACKYLENS_CAPABILITY_ACCELEROMETER_H
#define HACKYLENS_CAPABILITY_ACCELEROMETER_H

#include <hackylens/capability/common.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HK_CAPABILITY_ID_ACCELEROMETER UINT32_C(0x00010007)

typedef struct hk_accelerometer hk_accelerometer_t;

/* Native sensor axes in milli-g. Gravity contributes approximately 1000 mg. */
typedef struct
{
    int16_t x_mg;
    int16_t y_mg;
    int16_t z_mg;
} hk_acceleration_t;

const hk_accelerometer_t *hk_accelerometer_service(void);
/* Core 0 only. HK_PENDING indicates that the sensor is starting. The sample
 * is modified only on HK_OK. */
hk_result_t hk_accelerometer_read(const hk_accelerometer_t *accelerometer,
                                 hk_acceleration_t *sample);

#ifdef __cplusplus
}
#endif

#endif
