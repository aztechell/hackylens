#ifndef HK_AXP173_H
#define HK_AXP173_H

#include <stdint.h>

/* Preserve rails/charging; disable PEK automatic power-off for POW=BACK. */
uint8_t axp173_prepare(void);
/* Consume latched short/long PEK events; no held/repeat synthesis. */
uint8_t axp173_poll_back_press(uint64_t now_us);
/* Only explicit POWER OFF confirmation may invoke the shutdown control. */
uint8_t axp173_power_off(void);

/* Temporarily enable ADCs used by the monitor. The driver restores the added
 * channel bits when the monitor closes and retries a failed restore. */
void axp173_monitor_adc_begin(void);
uint8_t axp173_monitor_adc_end(void);
uint8_t axp173_read_registers(uint8_t reg, uint8_t *values, uint8_t count);

#endif
