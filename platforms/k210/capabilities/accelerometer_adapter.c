#include "../../../firmware/src/capabilities/accelerometer_provider.h"
#include "hal_onboard_i2c.h"
#include "hal_time.h"

/* MSA301, native axes, 14-bit resolution, +/-2 g and 62.5 Hz. */
#define MSA301_ADDRESS 0x26U
#define MSA301_PART_ID_REG 0x01U
#define MSA301_PART_ID 0x13U
#define MSA301_X_LOW 0x02U
#define MSA301_MOTION_STATUS 0x09U
#define MSA301_RESOLUTION_RANGE 0x0fU
#define MSA301_ODR 0x10U
#define MSA301_POWER_BANDWIDTH 0x11U
#define MSA301_MOTION_ENABLE 0x16U
#define MSA301_FREE_FALL_ENABLE 0x17U
#define MSA301_LATCH 0x21U
#define MSA301_ACTIVITY_THRESHOLD 0x28U
#define MSA301_MOTION_ORIENTATION 0x40U
#define MSA301_MOTION_TAP 0x20U
#define MSA301_MOTION_DOUBLE_TAP 0x10U
#define MSA301_ACTIVITY_AXES 0x07U
#define MSA301_MOTION_ACTIVITY 0x04U
#define MSA301_MOTION_FREE_FALL 0x02U
#define MSA301_ENABLE_FREE_FALL 0x08U
#define MSA301_LATCH_250_MS 0x01U
#define MSA301_ACTIVITY_250_MG 64U
#define MSA301_NORMAL_MODE 0x1eU
#define MSA301_14BIT_2G 0x00U
#define MSA301_62_5HZ 0x06U
#define MSA301_SETTLE_US 32000U
#define MSA301_COUNTS_PER_G 4096

static uint64_t ready_at;
static uint8_t motion_configured;

static int16_t milli_g(const uint8_t *bytes)
{
    int32_t counts = ((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8)) >> 2;
    if(counts & 0x2000)
        counts -= 0x4000;
    return (int16_t)(counts * 1000 / MSA301_COUNTS_PER_G);
}

static hk_result_t prepare_sensor(void)
{
    if(!ready_at)
    {
        uint8_t id;
        if(!hal_onboard_i2c_read(MSA301_ADDRESS, MSA301_PART_ID_REG, &id))
            return HK_ERR_IO;
        if(id != MSA301_PART_ID)
            return HK_ERR_FEATURE_UNAVAILABLE;
        if(!hal_onboard_i2c_write(MSA301_ADDRESS, MSA301_POWER_BANDWIDTH, MSA301_NORMAL_MODE) ||
           !hal_onboard_i2c_write(MSA301_ADDRESS, MSA301_RESOLUTION_RANGE, MSA301_14BIT_2G) ||
           !hal_onboard_i2c_write(MSA301_ADDRESS, MSA301_ODR, MSA301_62_5HZ))
            return HK_ERR_IO;
        ready_at = hal_time_us() + MSA301_SETTLE_US;
    }
    return hal_time_us() < ready_at ? HK_PENDING : HK_OK;
}

static hk_result_t read_sample(hk_acceleration_t *sample)
{
    hk_result_t result = prepare_sensor();
    if(result != HK_OK)
        return result;

    uint8_t axes[6];
    if(!hal_onboard_i2c_read_bytes(MSA301_ADDRESS, MSA301_X_LOW, axes, sizeof(axes)))
    {
        ready_at = 0U;
        motion_configured = 0U;
        return HK_ERR_IO;
    }
    *sample = (hk_acceleration_t){milli_g(axes), milli_g(axes + 2), milli_g(axes + 4)};
    return HK_OK;
}

static hk_result_t read_motion(hk_acceleration_motion_t *motion)
{
    hk_result_t result = prepare_sensor();
    if(result != HK_OK)
        return result;
    if(!motion_configured)
    {
        /* Leave INT1 routing alone. The app reads the latched flags over I2C. */
        const uint8_t enabled = MSA301_MOTION_ORIENTATION | MSA301_MOTION_TAP |
                                MSA301_MOTION_DOUBLE_TAP | MSA301_ACTIVITY_AXES;
        if(!hal_onboard_i2c_write(MSA301_ADDRESS, MSA301_LATCH, MSA301_LATCH_250_MS) ||
           !hal_onboard_i2c_write(MSA301_ADDRESS, MSA301_ACTIVITY_THRESHOLD, MSA301_ACTIVITY_250_MG) ||
           !hal_onboard_i2c_write(MSA301_ADDRESS, MSA301_MOTION_ENABLE, enabled) ||
           !hal_onboard_i2c_write(MSA301_ADDRESS, MSA301_FREE_FALL_ENABLE, MSA301_ENABLE_FREE_FALL))
            return HK_ERR_IO;
        motion_configured = 1U;
    }
    uint8_t status[4];
    if(!hal_onboard_i2c_read_bytes(MSA301_ADDRESS, MSA301_MOTION_STATUS, status, sizeof(status)))
    {
        motion_configured = 0U;
        return HK_ERR_IO;
    }
    uint8_t flags = 0U;
    if(status[0] & MSA301_MOTION_ACTIVITY) flags |= HK_ACCEL_MOTION_ACTIVITY;
    if(status[0] & MSA301_MOTION_TAP) flags |= HK_ACCEL_MOTION_TAP;
    if(status[0] & MSA301_MOTION_DOUBLE_TAP) flags |= HK_ACCEL_MOTION_DOUBLE_TAP;
    if(status[0] & MSA301_MOTION_FREE_FALL) flags |= HK_ACCEL_MOTION_FREE_FALL;
    if(status[0] & MSA301_MOTION_ORIENTATION) flags |= HK_ACCEL_MOTION_ORIENTATION;
    *motion = (hk_acceleration_motion_t){
        .flags = flags,
        .orientation = (hk_acceleration_orientation_t)((status[3] >> 4U) & 3U),
        .face_down = (status[3] >> 6U) & 1U,
    };
    return HK_OK;
}

const hk_accelerometer_t hk_accelerometer_binding = {
    .read = read_sample,
    .read_motion = read_motion,
};
