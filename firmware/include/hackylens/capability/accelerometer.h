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

enum
{
    HK_ACCEL_MOTION_ACTIVITY = 1U << 0,
    HK_ACCEL_MOTION_TAP = 1U << 1,
    HK_ACCEL_MOTION_DOUBLE_TAP = 1U << 2,
    HK_ACCEL_MOTION_FREE_FALL = 1U << 3,
    HK_ACCEL_MOTION_ORIENTATION = 1U << 4,
};

typedef enum
{
    HK_ACCEL_PORTRAIT_UPRIGHT,
    HK_ACCEL_PORTRAIT_INVERTED,
    HK_ACCEL_LANDSCAPE_LEFT,
    HK_ACCEL_LANDSCAPE_RIGHT,
} hk_acceleration_orientation_t;

typedef struct
{
    uint8_t flags;
    hk_acceleration_orientation_t orientation;
    uint8_t face_down;
} hk_acceleration_motion_t;

const hk_accelerometer_t *hk_accelerometer_service(void);
/* Core 0 only. HK_PENDING indicates that the sensor is starting. The sample
 * is modified only on HK_OK. */
hk_result_t hk_accelerometer_read(const hk_accelerometer_t *accelerometer,
                                 hk_acceleration_t *sample);
/* Core 0 only. Flags may stay active across several polls. Orientation is in
 * sensor coordinates and can be used after observing an orientation event. */
hk_result_t hk_accelerometer_read_motion(const hk_accelerometer_t *accelerometer,
                                        hk_acceleration_motion_t *motion);

#ifdef __cplusplus
}
#endif

#endif
