#ifndef HK_AXP173_MONITOR_H
#define HK_AXP173_MONITOR_H

#include <stdint.h>

#define AXP173_REGISTER_SPACE 0xb9U

typedef struct
{
    uint8_t values[AXP173_REGISTER_SPACE];
    uint8_t valid[(AXP173_REGISTER_SPACE + 7U) / 8U];
    uint8_t count;
} axp173_snapshot_t;

typedef struct
{
    axp173_snapshot_t sample;
    axp173_snapshot_t pending;
    uint8_t next_block;
} axp173_monitor_t;

enum
{
    AXP173_INPUT_STATUS = 0x00,
    AXP173_CHARGE_STATUS = 0x01,
    AXP173_OUTPUT_CONTROL = 0x12,
    AXP173_CHARGER_CONFIG = 0x33,
    AXP173_CHARGER_TIMERS = 0x34,
    AXP173_ADC_ENABLE1 = 0x82,
    AXP173_ADC_ENABLE2 = 0x83,
    AXP173_ADC_CONFIG = 0x84,
    AXP173_COULOMB_CONTROL = 0xb8,
};

enum
{
    AXP173_AC_PRESENT = 0x80U,
    AXP173_AC_VALID = 0x40U,
    AXP173_USB_PRESENT = 0x20U,
    AXP173_USB_VALID = 0x10U,
    AXP173_DIE_HOT = 0x80U,
    AXP173_CHARGING = 0x40U,
    AXP173_BATTERY_PRESENT = 0x20U,
};

typedef enum
{
    AXP173_BAT_UV,
    AXP173_CHARGE_UA,
    AXP173_DISCHARGE_UA,
    AXP173_BAT_UW,
    AXP173_AC_UV,
    AXP173_AC_UA,
    AXP173_USB_UV,
    AXP173_USB_UA,
    AXP173_SYS_UV,
    AXP173_DIE_DECIC,
    AXP173_TS_UV,
    AXP173_ADC_COUNT,
} axp173_measurement_t;

void axp173_monitor_start(axp173_monitor_t *monitor);
/* Reads one I2C block. A nonzero return publishes the completed snapshot. */
uint8_t axp173_monitor_step(axp173_monitor_t *monitor);
uint8_t axp173_monitor_stop(void);

uint8_t axp173_register_count(void);
uint8_t axp173_register_address(uint8_t index);
uint8_t axp173_snapshot_read(const axp173_snapshot_t *snapshot, uint8_t reg,
                             uint8_t count, uint32_t *value);
uint8_t axp173_measurement(const axp173_snapshot_t *snapshot,
                           axp173_measurement_t measurement, int32_t *value);
uint8_t axp173_battery_estimate(const axp173_snapshot_t *snapshot, uint8_t *percent);

#endif
