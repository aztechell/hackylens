#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <hackylens/capability/accelerometer.h>

static uint64_t now;
static uint8_t part_id = 0x12U;
static uint8_t config_writes;
static uint8_t read_fails;
static uint8_t motion_writes;
static uint8_t motion_fails;
static uint8_t motion_flags = 0x76U;
static uint8_t orientation = 0x60U;

uint64_t hal_time_us(void) { return now; }

uint8_t hal_onboard_i2c_read(uint8_t address, uint8_t reg, uint8_t *value)
{
    assert(address == 0x26U && reg == 0x01U);
    *value = part_id;
    return 1U;
}

uint8_t hal_onboard_i2c_write(uint8_t address, uint8_t reg, uint8_t value)
{
    static const uint8_t regs[] = {0x11U, 0x0fU, 0x10U};
    static const uint8_t values[] = {0x1eU, 0x00U, 0x06U};
    static const uint8_t motion_regs[] = {0x21U, 0x28U, 0x16U, 0x17U};
    static const uint8_t motion_values[] = {0x01U, 64U, 0x77U, 0x08U};
    assert(address == 0x26U);
    if(reg == regs[config_writes % 3U])
        assert(value == values[config_writes++ % 3U]);
    else
    {
        assert(reg == motion_regs[motion_writes % 4U]);
        assert(value == motion_values[motion_writes++ % 4U]);
    }
    return 1U;
}

uint8_t hal_onboard_i2c_read_bytes(uint8_t address, uint8_t reg, uint8_t *values,
                                  uint8_t count)
{
    static const uint8_t axes[] = {0x03U, 0xc0U, 0x00U, 0x40U, 0x00U, 0x20U};
    assert(address == 0x26U);
    if(reg == 0x09U)
    {
        assert(count == 4U);
        if(motion_fails)
            return 0U;
        values[0] = motion_flags;
        values[1] = values[2] = 0U;
        values[3] = orientation;
        return 1U;
    }
    assert(reg == 0x02U && count == sizeof(axes));
    if(read_fails)
        return 0U;
    memcpy(values, axes, sizeof(axes));
    return 1U;
}

int main(void)
{
    const hk_accelerometer_t *sensor = hk_accelerometer_service();
    hk_acceleration_t sample = {1, 2, 3};
    assert(sensor && hk_accelerometer_read(NULL, &sample) == HK_ERR_CAPABILITY_ABSENT);
    assert(hk_accelerometer_read(sensor, NULL) == HK_ERR_INVALID_ARGUMENT);
    assert(hk_accelerometer_read(sensor, &sample) == HK_ERR_FEATURE_UNAVAILABLE);
    assert(config_writes == 0U && sample.x_mg == 1);
    part_id = 0x13U;
    assert(hk_accelerometer_read(sensor, &sample) == HK_PENDING && config_writes == 3U);
    now = 31999U;
    assert(hk_accelerometer_read(sensor, &sample) == HK_PENDING && sample.z_mg == 3);
    now++;
    assert(hk_accelerometer_read(sensor, &sample) == HK_OK);
    assert(sample.x_mg == -1000 && sample.y_mg == 1000 && sample.z_mg == 500);
    read_fails = 1U;
    assert(hk_accelerometer_read(sensor, &sample) == HK_ERR_IO && sample.x_mg == -1000);
    read_fails = 0U;
    assert(hk_accelerometer_read(sensor, &sample) == HK_PENDING && config_writes == 6U);
    now += 32000U;
    assert(hk_accelerometer_read(sensor, &sample) == HK_OK && sample.y_mg == 1000);
    hk_acceleration_motion_t motion = {0};
    assert(hk_accelerometer_read_motion(NULL, &motion) == HK_ERR_CAPABILITY_ABSENT);
    assert(hk_accelerometer_read_motion(sensor, NULL) == HK_ERR_INVALID_ARGUMENT);
    assert(motion_writes == 0U);
    assert(hk_accelerometer_read_motion(sensor, &motion) == HK_OK && motion_writes == 4U);
    assert(motion.flags == (HK_ACCEL_MOTION_ACTIVITY | HK_ACCEL_MOTION_TAP |
           HK_ACCEL_MOTION_DOUBLE_TAP | HK_ACCEL_MOTION_FREE_FALL | HK_ACCEL_MOTION_ORIENTATION));
    assert(motion.orientation == HK_ACCEL_LANDSCAPE_LEFT && motion.face_down == 1U);
    motion_fails = 1U;
    assert(hk_accelerometer_read_motion(sensor, &motion) == HK_ERR_IO && motion.face_down == 1U);
    motion_fails = 0U;
    motion_flags = 0x10U;
    orientation = 0x10U;
    assert(hk_accelerometer_read_motion(sensor, &motion) == HK_OK && motion_writes == 8U);
    assert(motion.flags == HK_ACCEL_MOTION_DOUBLE_TAP);
    assert(motion.orientation == HK_ACCEL_PORTRAIT_INVERTED && motion.face_down == 0U);
    return 0;
}
