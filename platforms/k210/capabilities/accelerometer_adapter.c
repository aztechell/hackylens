#include "../../../firmware/src/capabilities/accelerometer_provider.h"
#include "hal_onboard_i2c.h"
#include "hal_time.h"

/* MSA301, native axes, 14-bit resolution, +/-2 g and 62.5 Hz. */
#define MSA301_ADDRESS 0x26U
#define MSA301_PART_ID_REG 0x01U
#define MSA301_PART_ID 0x13U
#define MSA301_X_LOW 0x02U
#define MSA301_RESOLUTION_RANGE 0x0fU
#define MSA301_ODR 0x10U
#define MSA301_POWER_BANDWIDTH 0x11U
#define MSA301_NORMAL_MODE 0x1eU
#define MSA301_14BIT_2G 0x00U
#define MSA301_62_5HZ 0x06U
#define MSA301_SETTLE_US 32000U
#define MSA301_COUNTS_PER_G 4096

static uint64_t ready_at;

static int16_t milli_g(const uint8_t *bytes)
{
    int32_t counts = ((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8)) >> 2;
    if(counts & 0x2000)
        counts -= 0x4000;
    return (int16_t)(counts * 1000 / MSA301_COUNTS_PER_G);
}

static hk_result_t read_sample(hk_acceleration_t *sample)
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
    if(hal_time_us() < ready_at)
        return HK_PENDING;

    uint8_t axes[6];
    if(!hal_onboard_i2c_read_bytes(MSA301_ADDRESS, MSA301_X_LOW, axes, sizeof(axes)))
    {
        ready_at = 0U;
        return HK_ERR_IO;
    }
    *sample = (hk_acceleration_t){milli_g(axes), milli_g(axes + 2), milli_g(axes + 4)};
    return HK_OK;
}

const hk_accelerometer_t hk_accelerometer_binding = {.read = read_sample};
