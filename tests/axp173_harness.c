#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "axp173.h"
#include "axp173_monitor.h"
static uint8_t regs[256];
static unsigned writes, reads;
static unsigned inits;
static uint8_t fail_read, fail_write;
static uint8_t fail_block = UINT8_MAX;
void hal_power_bus_init(void) { inits++; }
uint8_t hal_power_read_register(uint8_t reg, uint8_t *value)
{
    reads++;
    if(fail_read) return 0U;
    assert(reg == 0x00U || reg == 0x32U || reg == 0x36U || reg == 0x42U ||
           reg == 0x46U || reg == 0x82U || reg == 0x83U);
    *value = regs[reg];
    return 1U;
}
uint8_t hal_power_read_registers(uint8_t reg, uint8_t *values, uint8_t count)
{
    assert(count > 0U && count <= 7U);
    if(reg == fail_block) return 0U;
    memcpy(values, &regs[reg], count);
    return 1U;
}
uint8_t hal_power_write_register(uint8_t reg, uint8_t value)
{
    writes++;
    assert(reg == 0x32U || reg == 0x36U || reg == 0x42U || reg == 0x46U ||
           reg == 0x82U || reg == 0x83U);
    if(reg == 0x36U) assert(value == (regs[reg] & (uint8_t)~8U));
    if(reg == 0x82U) assert((value & 1U) == (regs[reg] & 1U));
    if(reg == 0x83U) assert((value & 0x7fU) == (regs[reg] & 0x7fU));
    if(fail_write) return 0U;
    if(reg == 0x46U)
    {
        assert((value & ~3U) == 0U);
        regs[reg] &= (uint8_t)~value;
    }
    else
        regs[reg] = value;
    return 1U;
}

static void scan(axp173_monitor_t *monitor)
{
    for(unsigned step = 0U; step < 25U; step++)
        assert(!axp173_monitor_step(monitor));
    assert(axp173_monitor_step(monitor));
}

static void test_monitor(void)
{
    axp173_monitor_t monitor;
    uint8_t baseline[sizeof(regs)], percent;
    uint32_t value;
    int32_t measurement;
    regs[0x01] = AXP173_BATTERY_PRESENT;
    regs[0x5e] = 0x51;
    regs[0x5f] = 0x04;
    regs[0x71] = 0x03;
    regs[0x72] = 0xe8;
    regs[0x78] = 0xd7;
    regs[0x79] = 0xff;
    regs[0x7a] = 0x06;
    regs[0x7b] = 0xe7;
    regs[0x82] = 0x82; /* TS disabled, battery and APS already active. */
    regs[0x83] = 0x21;
    regs[0xb0] = 1;
    regs[0xb1] = 2;
    regs[0xb2] = 3;
    regs[0xb3] = 4;
    memcpy(baseline, regs, sizeof(regs));

    axp173_monitor_start(&monitor);
    assert(regs[0x82] == 0xfe && regs[0x83] == 0xa1);
    assert(!axp173_snapshot_read(&monitor.sample, 0x00, 1U, &value));
    scan(&monitor);
    assert(monitor.sample.count == 78U && axp173_register_count() == 78U);
    assert(axp173_register_address(77U) == 0xb8U && axp173_register_address(78U) == UINT8_MAX);
    assert(axp173_snapshot_read(&monitor.sample, 0x4a, 1U, &value));
    assert(axp173_snapshot_read(&monitor.sample, 0x4d, 1U, &value));
    assert(!axp173_snapshot_read(&monitor.sample, 0x48, 1U, &value));
    assert(axp173_snapshot_read(&monitor.sample, 0xb0, 4U, &value) && value == 0x01020304U);
    assert(axp173_measurement(&monitor.sample, AXP173_BAT_UV, &measurement) && measurement == 3800500);
    assert(axp173_measurement(&monitor.sample, AXP173_CHARGE_UA, &measurement) && measurement == 99500);
    assert(axp173_measurement(&monitor.sample, AXP173_BAT_UW, &measurement) && measurement == 550);
    assert(axp173_measurement(&monitor.sample, AXP173_DIE_DECIC, &measurement) && measurement == -147);
    assert(!axp173_measurement(&monitor.sample, AXP173_TS_UV, &measurement));
    assert(axp173_battery_estimate(&monitor.sample, &percent) && percent == 45U);

    regs[0x01] = 0U;
    scan(&monitor);
    assert(!axp173_battery_estimate(&monitor.sample, &percent));
    regs[0x01] = baseline[0x01];
    fail_block = 0x78;
    scan(&monitor);
    assert(monitor.sample.count == 72U);
    assert(!axp173_measurement(&monitor.sample, AXP173_BAT_UV, &measurement));
    fail_block = UINT8_MAX;

    fail_write = 1U;
    assert(!axp173_monitor_stop());
    assert(regs[0x82] == 0xfe && regs[0x83] == 0xa1);
    fail_write = 0U;
    assert(!axp173_poll_back_press(3540000U));
    assert(memcmp(baseline, regs, sizeof(regs)) == 0);
}

int main(void)
{
    regs[0] = 0xC1U;
    regs[0x36U] = 0x5DU;
    regs[0x42U] = 0xA1U;
    regs[0x46U] = 0x83U;
    assert(axp173_prepare());
    assert(regs[0x36U] == 0x55U);
    assert(regs[0x42U] == 0xA3U && regs[0x46U] == 0x80U);
    assert(!axp173_poll_back_press(0U)); /* Boot PEK is not a BACK. */
    regs[0x46U] |= 2U;
    assert(axp173_poll_back_press(20000U));
    assert(regs[0x46U] == 0x80U);
    unsigned previous = reads;
    assert(!axp173_poll_back_press(25000U) && reads == previous);
    assert(!axp173_poll_back_press(40000U));
    regs[0x46U] |= 1U; /* Long press is also BACK, never power-off. */
    assert(axp173_poll_back_press(60000U));
    assert(regs[0x46U] == 0x80U);
    regs[0x46U] |= 2U;
    fail_write = 1U;
    assert(axp173_poll_back_press(80000U));
    assert(!axp173_poll_back_press(100000U)); /* Failed W1C cannot repeat. */
    fail_write = 0U;
    assert(!axp173_poll_back_press(120000U));
    assert(regs[0x46U] == 0x80U);
    regs[0x46U] |= 2U;
    fail_read = 1U;
    assert(!axp173_poll_back_press(140000U));
    fail_read = 0U;
    assert(!axp173_poll_back_press(160000U)); /* Bus-failure backoff. */
    assert(axp173_poll_back_press(240000U)); /* Latched tap survives outage. */
    assert(regs[0x46U] == 0x80U && writes > 0U);
    /* A transient boot probe failure must not disable POW forever. A latched
     * event predating successful setup is cleared, not reported as BACK. */
    fail_read = 1U;
    assert(!axp173_prepare());
    previous = reads;
    assert(!axp173_poll_back_press(1000000U));
    assert(!axp173_poll_back_press(1499999U) && reads == previous);
    assert(!axp173_poll_back_press(1500000U));
    assert(reads > previous);
    previous = reads;
    fail_read = 0U;
    regs[0x46U] |= 3U;
    assert(!axp173_poll_back_press(1999999U) && reads == previous);
    assert(!axp173_poll_back_press(2000000U));
    assert(regs[0x46U] == 0x80U && inits >= 4U);
    assert(!axp173_poll_back_press(2020000U));
    regs[0x46U] |= 2U;
    assert(axp173_poll_back_press(2040000U));
    assert(!axp173_poll_back_press(2060000U));
    /* Present PMIC with failed PEK setup follows the same bounded retry. */
    fail_write = 1U;
    assert(axp173_prepare());
    assert(!axp173_poll_back_press(3000000U));
    fail_write = 0U;
    regs[0x46U] |= 2U;
    assert(!axp173_poll_back_press(3500000U));
    assert(regs[0x46U] == 0x80U);
    regs[0x46U] |= 1U;
    assert(axp173_poll_back_press(3520000U));
    test_monitor();
    regs[0x32U] = 0x46U;
    assert(axp173_power_off());
    assert(regs[0x32U] == 0xC6U);
    puts("AXP173_OK short-edge long-back auto-off-disabled w1c retry no-rail-writes");
    return 0;
}
