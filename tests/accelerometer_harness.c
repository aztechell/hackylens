#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <hackylens/capability/accelerometer.h>

static uint64_t now;
static uint8_t part_id = 0x12U;
static uint8_t config_writes;
static uint8_t read_fails;

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
    assert(address == 0x26U && reg == regs[config_writes % 3U]);
    assert(value == values[config_writes++ % 3U]);
    return 1U;
}

uint8_t hal_onboard_i2c_read_bytes(uint8_t address, uint8_t reg, uint8_t *values,
                                  uint8_t count)
{
    static const uint8_t axes[] = {0x03U, 0xc0U, 0x00U, 0x40U, 0x00U, 0x20U};
    assert(address == 0x26U && reg == 0x02U && count == sizeof(axes));
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
    return 0;
}
