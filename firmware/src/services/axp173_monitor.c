#include "hk_config.h"

#if HK_ENABLE_APP_AXP173
#include "axp173_monitor.h"
#include "../drivers/axp173.h"

#include <string.h>

typedef struct { uint8_t first, size; } register_block_t;

/* AXP173 datasheet sections 9.8–9.11. Exclude reserved addresses and keep
 * each ADC reading or counter within one transaction. I2C1 accepts 7 bytes. */
static const register_block_t blocks[] = {
    {0x00, 2}, {0x04, 1}, {0x06, 6}, {0x10, 1}, {0x12, 1},
    {0x23, 1}, {0x25, 4}, {0x30, 5}, {0x36, 7}, {0x3d, 1},
    {0x40, 4}, {0x44, 4}, {0x4a, 1}, {0x4d, 1},
    {0x56, 6}, {0x5c, 4}, {0x62, 2}, {0x70, 3}, {0x78, 6}, {0x7e, 2},
    {0x80, 1}, {0x82, 3}, {0x8a, 2}, {0x8f, 1},
    {0xb0, 4}, {0xb4, 5},
};

typedef struct
{
    uint8_t reg, size, low_bits, enable_reg, mask;
    uint16_t scale, divisor;
    int16_t offset;
} channel_t;

static const channel_t channels[AXP173_ADC_COUNT] = {
    [AXP173_BAT_UV]       = {0x78, 2, 4, AXP173_ADC_ENABLE1, 0x80, 1100, 1, 0},
    [AXP173_CHARGE_UA]    = {0x7a, 2, 5, AXP173_ADC_ENABLE1, 0x40,  500, 1, 0},
    [AXP173_DISCHARGE_UA] = {0x7c, 2, 5, AXP173_ADC_ENABLE1, 0x40,  500, 1, 0},
    [AXP173_BAT_UW]       = {0x70, 3, 8, AXP173_ADC_ENABLE1, 0xc0,   55, 100, 0},
    [AXP173_AC_UV]        = {0x56, 2, 4, AXP173_ADC_ENABLE1, 0x20, 1700, 1, 0},
    [AXP173_AC_UA]        = {0x58, 2, 4, AXP173_ADC_ENABLE1, 0x10,  625, 1, 0},
    [AXP173_USB_UV]       = {0x5a, 2, 4, AXP173_ADC_ENABLE1, 0x08, 1700, 1, 0},
    [AXP173_USB_UA]       = {0x5c, 2, 4, AXP173_ADC_ENABLE1, 0x04,  375, 1, 0},
    [AXP173_SYS_UV]       = {0x7e, 2, 4, AXP173_ADC_ENABLE1, 0x02, 1400, 1, 0},
    [AXP173_DIE_DECIC]    = {0x5e, 2, 4, AXP173_ADC_ENABLE2, 0x80,    1, 1, -1447},
    [AXP173_TS_UV]        = {0x62, 2, 4, AXP173_ADC_ENABLE1, 0x01,  800, 1, 0},
};

void axp173_monitor_start(axp173_monitor_t *monitor)
{
    memset(monitor, 0, sizeof(*monitor));
    axp173_monitor_adc_begin();
}

uint8_t axp173_monitor_step(axp173_monitor_t *monitor)
{
    const register_block_t block = blocks[monitor->next_block++];
    axp173_snapshot_t *pending = &monitor->pending;
    if(axp173_read_registers(block.first, &pending->values[block.first], block.size))
    {
        for(uint8_t reg = block.first; reg < block.first + block.size; reg++)
            pending->valid[reg / 8U] |= 1U << (reg % 8U);
        pending->count += block.size;
    }
    if(monitor->next_block < sizeof(blocks) / sizeof(blocks[0]))
        return 0U;

    monitor->sample = *pending;
    pending->count = 0U;
    memset(pending->valid, 0, sizeof(pending->valid));
    monitor->next_block = 0U;
    return 1U;
}

uint8_t axp173_monitor_stop(void)
{
    return axp173_monitor_adc_end();
}

uint8_t axp173_register_count(void)
{
    uint8_t count = 0U;
    for(unsigned i = 0U; i < sizeof(blocks) / sizeof(blocks[0]); i++)
        count += blocks[i].size;
    return count;
}

uint8_t axp173_register_address(uint8_t index)
{
    for(unsigned i = 0U; i < sizeof(blocks) / sizeof(blocks[0]); i++)
    {
        if(index < blocks[i].size)
            return blocks[i].first + index;
        index -= blocks[i].size;
    }
    return UINT8_MAX;
}

uint8_t axp173_snapshot_read(const axp173_snapshot_t *snapshot, uint8_t reg,
                             uint8_t count, uint32_t *value)
{
    if(count == 0U || count > sizeof(*value) || reg + count > AXP173_REGISTER_SPACE)
        return 0U;
    uint32_t result = 0U;
    for(uint8_t next = reg; next < reg + count; next++)
    {
        if(!(snapshot->valid[next / 8U] & (1U << (next % 8U))))
            return 0U;
        result = (result << 8U) | snapshot->values[next];
    }
    *value = result;
    return 1U;
}

uint8_t axp173_measurement(const axp173_snapshot_t *snapshot,
                           axp173_measurement_t measurement, int32_t *value)
{
    if((unsigned)measurement >= AXP173_ADC_COUNT)
        return 0U;
    const channel_t *channel = &channels[measurement];
    uint32_t enable, raw;
    if(!axp173_snapshot_read(snapshot, channel->enable_reg, 1U, &enable) ||
       (enable & channel->mask) != channel->mask ||
       !axp173_snapshot_read(snapshot, channel->reg, channel->size, &raw))
        return 0U;
    uint32_t low_mask = (1U << channel->low_bits) - 1U;
    raw = ((raw >> 8U) << channel->low_bits) | (raw & low_mask);
    *value = (int32_t)((raw * channel->scale + channel->divisor / 2U) / channel->divisor) +
             channel->offset;
    return 1U;
}

uint8_t axp173_battery_estimate(const axp173_snapshot_t *snapshot, uint8_t *percent)
{
    /* Approximate Li-Po voltage curve. The UI displays this as an estimate. */
    static const struct { uint16_t mv; uint8_t percent; } curve[] = {
        {3400, 0}, {3600, 10}, {3700, 20}, {3800, 45},
        {3900, 65}, {4050, 85}, {4200, 100},
    };
    int32_t uv;
    uint32_t status;
    if(!axp173_snapshot_read(snapshot, AXP173_CHARGE_STATUS, 1U, &status) ||
       !(status & AXP173_BATTERY_PRESENT) ||
       !axp173_measurement(snapshot, AXP173_BAT_UV, &uv) || uv < 2500000 || uv > 4500000)
        return 0U;
    uint16_t mv = (uint16_t)(uv / 1000);
    *percent = 0U;
    if(mv <= curve[0].mv)
        return 1U;
    for(unsigned i = 1U; i < sizeof(curve) / sizeof(curve[0]); i++)
    {
        if(mv <= curve[i].mv)
        {
            *percent = curve[i - 1U].percent +
                (mv - curve[i - 1U].mv) * (curve[i].percent - curve[i - 1U].percent) /
                (curve[i].mv - curve[i - 1U].mv);
            return 1U;
        }
    }
    *percent = 100U;
    return 1U;
}
#endif
