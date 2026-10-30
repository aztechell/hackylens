#include "axp173.h"
#include "hal_power.h"

#include <stdio.h>

/* X-Powers AXP173 datasheet pp. 36, 41-42: PEK auto-off enable,
 * IRQ3 short/long PEK, W1C status. Preserve rails and all timing fields. */
#define AXP173_PEK_CONFIG 0x36U
#define AXP173_PEK_AUTO_OFF 0x08U
#define AXP173_IRQ_ENABLE3 0x42U
#define AXP173_IRQ_STATUS3 0x46U
#define AXP173_PEK_EVENTS 0x03U
#define AXP173_POLL_US 20000ULL
#define AXP173_RETRY_US 100000ULL
#define AXP173_PREPARE_RETRY_US 500000ULL
static uint8_t s_pek_ready;
static uint8_t s_probe_backoff_armed;
static uint8_t s_consumed_events;
static uint64_t s_next_poll_us;

static uint8_t prepare_pek(uint8_t log_result)
{
    uint8_t status;
    uint8_t enabled;
    uint8_t pek;
    uint8_t verified;
    s_pek_ready = 0U;

    hal_power_bus_init();
    if(!hal_power_read_register(0x00U, &status))
    {
        if(log_result)
            printf("[POWER] AXP173 management bus unavailable\r\n");
        return 0U;
    }
    /* Cube's M1n uses its own fixed regulators. Do not copy another board's
     * AXP rail voltages or battery charge-current configuration. */
    if(log_result)
        printf("[POWER] AXP173 status=0x%02X rails=power-on-defaults\r\n", status);
    if(hal_power_read_register(AXP173_PEK_CONFIG, &pek) &&
       (!(pek & AXP173_PEK_AUTO_OFF) ||
        hal_power_write_register(AXP173_PEK_CONFIG, pek & (uint8_t)~AXP173_PEK_AUTO_OFF)) &&
       hal_power_read_register(AXP173_PEK_CONFIG, &verified) &&
       !(verified & AXP173_PEK_AUTO_OFF) &&
       hal_power_read_register(AXP173_IRQ_ENABLE3, &enabled) &&
       ((enabled & AXP173_PEK_EVENTS) == AXP173_PEK_EVENTS ||
        hal_power_write_register(AXP173_IRQ_ENABLE3, enabled | AXP173_PEK_EVENTS)) &&
       hal_power_write_register(AXP173_IRQ_STATUS3, AXP173_PEK_EVENTS))
        s_pek_ready = 1U;
    if(log_result)
        printf("[POWER] POW=BACK auto-off=disabled %s\r\n",
               s_pek_ready ? "verified" : "setup-failed");
    return 1U;
}

uint8_t axp173_prepare(void)
{
    s_probe_backoff_armed = 0U;
    s_consumed_events = 0U;
    s_next_poll_us = 0U;
    return prepare_pek(1U);
}

uint8_t axp173_poll_back_press(uint64_t now_us)
{
    uint8_t status;
    uint8_t pressed;
    uint8_t events;
    if(now_us < s_next_poll_us)
        return 0U;
    if(!s_pek_ready)
    {
        /* Establish backoff using the polling clock; startup may precede the
         * input service. Recovery clears stale PEK latches, never emits BACK. */
        if(s_probe_backoff_armed)
        {
            (void)prepare_pek(0U);
            if(s_pek_ready)
            {
                s_consumed_events = 0U;
                printf("[POWER] POW=BACK recovered auto-off=disabled verified\r\n");
            }
        }
        s_probe_backoff_armed = 1U;
        s_next_poll_us = now_us + (s_pek_ready ? AXP173_POLL_US : AXP173_PREPARE_RETRY_US);
        return 0U;
    }
    s_next_poll_us = now_us + AXP173_POLL_US;
    if(!hal_power_read_register(AXP173_IRQ_STATUS3, &status))
    {
        s_next_poll_us = now_us + AXP173_RETRY_US;
        return 0U;
    }
    events = status & AXP173_PEK_EVENTS;
    pressed = (uint8_t)((events & (uint8_t)~s_consumed_events) != 0U);
    s_consumed_events = events;
    /* A failed acknowledgement must not turn one latched event into repeats. */
    if(events && hal_power_write_register(AXP173_IRQ_STATUS3, events))
        s_consumed_events = 0U;
    return pressed;
}

uint8_t axp173_power_off(void)
{
    uint8_t shutdown;
    /* X-Powers AXP173 datasheet p.35: REG32[7] closes the PMIC outputs. */
    return (uint8_t)(hal_power_read_register(0x32U, &shutdown) &&
                    hal_power_write_register(0x32U, shutdown | 0x80U));
}
