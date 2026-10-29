#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "axp173.h"
static uint8_t regs[256];
static unsigned writes, reads;
static uint8_t fail_read, fail_write;
void hal_power_bus_init(void) {}
uint8_t hal_power_read_register(uint8_t reg, uint8_t *value)
{
    reads++;
    if(fail_read) return 0U;
    assert(reg == 0x00U || reg == 0x32U || reg == 0x36U || reg == 0x42U || reg == 0x46U);
    *value = regs[reg];
    return 1U;
}
uint8_t hal_power_write_register(uint8_t reg, uint8_t value)
{
    writes++;
    /* Only the exact auto-off bit and PEK IRQ configuration can change. */
    assert(reg == 0x32U || reg == 0x36U || reg == 0x42U || reg == 0x46U);
    if(reg == 0x36U) assert(value == (regs[reg] & (uint8_t)~8U));
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
    regs[0x32U] = 0x46U;
    assert(axp173_power_off());
    assert(regs[0x32U] == 0xC6U);
    puts("AXP173_OK short-edge long-back auto-off-disabled w1c retry no-rail-writes");
    return 0;
}
