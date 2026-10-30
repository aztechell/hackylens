#include "hal_power.h"
#include "hal_time.h"

#include <i2c.h>
#include <platform.h>
#include <sysctl.h>
#include <fpioa.h>
#include <gpiohs.h>
#include <encoding.h>
#include <stdio.h>
#include "board_config.h"

#define POWER_BUS_TIMEOUT_US 20000ULL
#ifndef POWER_BUS_READ
#define POWER_BUS_READ(field) (((volatile i2c_t *)I2C1_BASE_ADDR)->field)
#define POWER_BUS_WRITE(field, value) (((volatile i2c_t *)I2C1_BASE_ADDR)->field = (value))
#endif
#ifndef POWER_GPIO_READ
#define POWER_GPIO_READ(field) (((volatile gpiohs_raw_t *)GPIOHS_BASE_ADDR)->field)
#define POWER_GPIO_WRITE(field, value) (((volatile gpiohs_raw_t *)GPIOHS_BASE_ADDR)->field = (value))
#endif
#ifndef POWER_IRQ_SAVE
#define POWER_IRQ_SAVE() clear_csr(mstatus, MSTATUS_MIE)
#define POWER_IRQ_RESTORE(state) do { if((state) & MSTATUS_MIE) (void)set_csr(mstatus, MSTATUS_MIE); } while(0)
#endif
/* GPIOHS is always-on, with no SYSCTL clock gate. Cube uses channels 2/3/4,
 * 6/14/15; borrow two currently unmapped channels, preferring the upper half.
 * Maximum pulse/stretch budget is 500 us, plus fixed scan/save/restore work.
 * IRQs stay masked through cleanup to make shared GPIO RMW and IP hygiene safe. */
#define POWER_CLEAR_BUDGET_US 500U

static uint8_t clear_delay(uint64_t deadline)
{
    uint64_t until = hal_time_us() + 5U;
    for(;;) {
        uint64_t now = hal_time_us();
        if(now >= deadline) return 0U;
        if(now >= until) return 1U;
    }
}
static uint8_t clear_scl_high(uint64_t deadline, uint32_t scl_mask)
{
    while(!(POWER_GPIO_READ(input_val) & scl_mask))
        if(hal_time_us() >= deadline) return 0U;
    return clear_delay(deadline);
}
static uint8_t power_bus_clear(hal_power_failure_stage_t *failure_stage)
{
    fpioa_io_config_t scl, sda, cfg;
    gpiohs_raw_t saved;
    unsigned long irq;
    uint64_t deadline;
    uint8_t success = 0U, pulses = 0U;
    uint32_t used = 0U, scl_mask = 0U, sda_mask = 0U, mask;
    unsigned scl_channel = 0U, sda_channel = 0U;
    *failure_stage = HAL_POWER_BUS_CLEAR;
    if(fpioa_get_io(IO_PMIC_SCL, &scl) || fpioa_get_io(IO_PMIC_SDA, &sda)) return 0U;
    if(scl.pad_di && sda.pad_di) return 1U;
    irq = POWER_IRQ_SAVE();
    __sync_synchronize();
    deadline = hal_time_us() + POWER_CLEAR_BUDGET_US;
    for(unsigned pin = 0; pin < FPIOA_NUM_IO; pin++) {
        if(fpioa_get_io((int)pin, &cfg)) {
            *failure_stage = HAL_POWER_GPIO_COLLISION;
            POWER_IRQ_RESTORE(irq);
            return 0U;
        }
        if(cfg.ch_sel >= FUNC_GPIOHS0 && cfg.ch_sel <= FUNC_GPIOHS31)
            used |= 1UL << (cfg.ch_sel - FUNC_GPIOHS0);
    }
    /* Prefer 30/31, but warm-reset FPIOA defaults may still map them elsewhere. */
    used |= (1UL << 2) | (1UL << 3) | (1UL << 4) | (1UL << 6) |
            (1UL << 14) | (1UL << 15);
    for(unsigned i = 0; i < 32U; i++) {
        unsigned channel = i < 2U ? 30U + i : 31U - i;
        uint32_t bit = 1UL << channel;
        if(!(used & bit)) {
            if(!scl_mask) { scl_mask = bit; scl_channel = channel; }
            else { sda_mask = bit; sda_channel = channel; break; }
        }
    }
    if(!sda_mask) {
        *failure_stage = HAL_POWER_GPIO_COLLISION;
        POWER_IRQ_RESTORE(irq);
        return 0U;
    }
    mask = scl_mask | sda_mask;
    if(scl.ch_sel != IO_PMIC_SCL_FUNCTION || sda.ch_sel != IO_PMIC_SDA_FUNCTION) {
        *failure_stage = HAL_POWER_GPIO_COLLISION;
        POWER_IRQ_RESTORE(irq);
        return 0U;
    }
#define SAVE(field) saved.field = POWER_GPIO_READ(field)
    SAVE(input_en); SAVE(output_en); SAVE(output_val); SAVE(output_xor);
    SAVE(iof_en); SAVE(iof_sel); SAVE(pullup_en); SAVE(drive);
    SAVE(rise_ie); SAVE(fall_ie); SAVE(high_ie); SAVE(low_ie);
    SAVE(rise_ip); SAVE(fall_ip); SAVE(high_ip); SAVE(low_ip);
#undef SAVE
#define MASK_OFF(field) POWER_GPIO_WRITE(field, POWER_GPIO_READ(field) & ~mask)
    MASK_OFF(rise_ie); MASK_OFF(fall_ie); MASK_OFF(high_ie); MASK_OFF(low_ie);
    /* Value is LOW before enabling any output. HIGH always means high-Z. */
    MASK_OFF(output_en); MASK_OFF(output_val); MASK_OFF(output_xor); MASK_OFF(iof_en);
    POWER_GPIO_WRITE(input_en, POWER_GPIO_READ(input_en) | mask);
    cfg = scl; cfg.ch_sel = FUNC_GPIOHS0 + scl_channel;
    cfg.oe_en = 1U; cfg.oe_inv = 0U; cfg.do_sel = 0U; cfg.do_inv = 0U;
    cfg.ie_en = 1U; cfg.ie_inv = 0U; cfg.di_inv = 0U; cfg.pu = 1U; cfg.pd = 0U;
    if(fpioa_set_io(IO_PMIC_SCL, &cfg)) goto restore;
    cfg.ch_sel = FUNC_GPIOHS0 + sda_channel;
    if(fpioa_set_io(IO_PMIC_SDA, &cfg)) goto restore;
    if(!clear_scl_high(deadline, scl_mask)) goto restore;
    while(pulses < 9U && !(POWER_GPIO_READ(input_val) & sda_mask)) {
        POWER_GPIO_WRITE(output_en, POWER_GPIO_READ(output_en) | scl_mask);
        if(!clear_delay(deadline)) goto restore;
        MASK_OFF(output_en); /* release SCL, and SDA remains released */
        pulses++;
        if(!clear_scl_high(deadline, scl_mask)) goto restore;
    }
    /* STOP: SDA low while SCL low; release SCL, then release SDA. */
    POWER_GPIO_WRITE(output_en, POWER_GPIO_READ(output_en) | scl_mask);
    if(!clear_delay(deadline)) goto restore;
    POWER_GPIO_WRITE(output_en, POWER_GPIO_READ(output_en) | sda_mask);
    if(!clear_delay(deadline)) goto restore;
    POWER_GPIO_WRITE(output_en, POWER_GPIO_READ(output_en) & ~scl_mask);
    if(!clear_scl_high(deadline, scl_mask)) goto restore;
    MASK_OFF(output_en);
    if(!clear_delay(deadline)) goto restore;
    success = (POWER_GPIO_READ(input_val) & mask) == mask;
restore:
    MASK_OFF(output_en);
    (void)fpioa_set_io(IO_PMIC_SCL, &scl);
    (void)fpioa_set_io(IO_PMIC_SDA, &sda);
#define RESTORE(field) POWER_GPIO_WRITE(field, (POWER_GPIO_READ(field) & ~mask) | (saved.field & mask))
    RESTORE(output_val); RESTORE(output_xor); RESTORE(iof_en); RESTORE(iof_sel);
    RESTORE(pullup_en); RESTORE(drive); RESTORE(input_en); RESTORE(output_en);
    /* W1C only newly generated pending bits; preserve all pre-existing IPs. */
#define CLEAR_NEW(field) POWER_GPIO_WRITE(field, POWER_GPIO_READ(field) & ~saved.field & mask)
    CLEAR_NEW(rise_ip); CLEAR_NEW(fall_ip); CLEAR_NEW(high_ip); CLEAR_NEW(low_ip);
#undef CLEAR_NEW
    RESTORE(rise_ie); RESTORE(fall_ie); RESTORE(high_ie); RESTORE(low_ie);
#undef RESTORE
#undef MASK_OFF
    __sync_synchronize();
    POWER_IRQ_RESTORE(irq);
    if(success) printf("[POWERBUS] bus-clear recovered pulses=%u gpiohs=%u/%u\r\n",
                       pulses, scl_channel, sda_channel);
    return success;
}
static uint8_t s_bus_ready;
static hal_power_diagnostic_t s_diagnostic;

void hal_power_get_diagnostic(hal_power_diagnostic_t *diagnostic)
{
    if(diagnostic) *diagnostic = s_diagnostic;
}

static void failed(hal_power_failure_stage_t stage, uint8_t reg)
{
    fpioa_io_config_t scl = {0}, sda = {0};
    s_diagnostic.failures++;
    s_diagnostic.stage = (uint8_t)stage;
    s_diagnostic.reg = reg;
    s_diagnostic.abort_source = POWER_BUS_READ(tx_abrt_source);
    s_diagnostic.status = POWER_BUS_READ(status);
    s_diagnostic.raw_intr = POWER_BUS_READ(raw_intr_stat);
    s_diagnostic.txflr = POWER_BUS_READ(txflr);
    s_diagnostic.rxflr = POWER_BUS_READ(rxflr);
    s_diagnostic.enable = POWER_BUS_READ(enable);
    s_diagnostic.enable_status = POWER_BUS_READ(enable_status);
    s_diagnostic.con = POWER_BUS_READ(con);
    s_diagnostic.tar = POWER_BUS_READ(tar);
    s_diagnostic.hcnt = POWER_BUS_READ(ss_scl_hcnt);
    s_diagnostic.lcnt = POWER_BUS_READ(ss_scl_lcnt);
    (void)fpioa_get_io(IO_PMIC_SCL, &scl);
    (void)fpioa_get_io(IO_PMIC_SDA, &sda);
    s_diagnostic.scl_pad = scl.pad_di;
    s_diagnostic.sda_pad = sda.pad_di;
    s_diagnostic.scl_function = scl.ch_sel;
    s_diagnostic.sda_function = sda.ch_sel;
    /* Capture first: disabling or reading CLR_TX_ABRT destroys evidence. */
    s_bus_ready = 0U;
    POWER_BUS_WRITE(enable, 0U);
    (void)POWER_BUS_READ(clr_tx_abrt);
    /* Persistent absent/stuck hardware must not flood the debug transport. */
    if((s_diagnostic.failures & (s_diagnostic.failures - 1U)) == 0U)
        printf("[POWERBUS] n=%u stage=%u reg=%02X abort=%08X status=%08X raw=%08X tx=%u rx=%u en=%u/%u con=%08X tar=%02X hc=%u lc=%u SCL=%u/f%u SDA=%u/f%u\r\n",
            (unsigned)s_diagnostic.failures, (unsigned)stage, reg,
            (unsigned)s_diagnostic.abort_source, (unsigned)s_diagnostic.status,
            (unsigned)s_diagnostic.raw_intr, (unsigned)s_diagnostic.txflr,
            (unsigned)s_diagnostic.rxflr, (unsigned)s_diagnostic.enable,
            (unsigned)s_diagnostic.enable_status, (unsigned)s_diagnostic.con,
            (unsigned)s_diagnostic.tar, (unsigned)s_diagnostic.hcnt,
            (unsigned)s_diagnostic.lcnt, s_diagnostic.scl_pad,
            s_diagnostic.scl_function, s_diagnostic.sda_pad, s_diagnostic.sda_function);
}

void hal_power_bus_init(void)
{
    uint64_t deadline = hal_time_us() + POWER_BUS_TIMEOUT_US;
    s_bus_ready = 0U;
    /* On the first boot probe the SDK has not enabled I2C1's clock yet. */
    sysctl_clock_enable(SYSCTL_CLOCK_I2C1);
    POWER_BUS_WRITE(enable, 0U);
    while(POWER_BUS_READ(enable_status) & I2C_ENABLE_STATUS_IC_ENABLE)
    {
        if(hal_time_us() >= deadline)
        {
            failed(HAL_POWER_INIT_DISABLE, 0U);
            return;
        }
    }
    hal_power_failure_stage_t clear_failure;
    if(!power_bus_clear(&clear_failure)) {
        failed(clear_failure, 0U);
        return;
    }
    i2c_init(I2C_DEVICE_1, 0x34U, 7U, 100000U);
    while(!(POWER_BUS_READ(enable_status) & I2C_ENABLE_STATUS_IC_ENABLE))
    {
        if(hal_time_us() >= deadline)
        {
            failed(HAL_POWER_INIT_ENABLE, 0U);
            return;
        }
    }
    s_bus_ready = 1U;
}

uint8_t hal_power_read_register(uint8_t reg, uint8_t *value)
{
    uint64_t deadline;
    uint8_t result;
    hal_power_failure_stage_t stage = HAL_POWER_RX_DRAIN;

    if(!value)
        return 0U;
    if(!s_bus_ready) hal_power_bus_init();
    if(!s_bus_ready) return 0U;
    deadline = hal_time_us() + POWER_BUS_TIMEOUT_US;
    POWER_BUS_WRITE(enable, I2C_ENABLE_ENABLE);
    (void)POWER_BUS_READ(clr_tx_abrt);
    while(POWER_BUS_READ(rxflr))
    {
        if(hal_time_us() >= deadline)
            goto failed;
        (void)POWER_BUS_READ(data_cmd);
    }
    POWER_BUS_WRITE(data_cmd, reg);
    POWER_BUS_WRITE(data_cmd, I2C_DATA_CMD_CMD);
    stage = HAL_POWER_RX_WAIT;
    while(POWER_BUS_READ(rxflr) == 0U)
    {
        if(POWER_BUS_READ(tx_abrt_source) || hal_time_us() >= deadline)
            goto failed;
    }
    result = (uint8_t)POWER_BUS_READ(data_cmd);
    stage = HAL_POWER_RX_IDLE;
    while((POWER_BUS_READ(status) & I2C_STATUS_ACTIVITY) ||
          !(POWER_BUS_READ(status) & I2C_STATUS_TFE))
    {
        if(POWER_BUS_READ(tx_abrt_source) || hal_time_us() >= deadline)
            goto failed;
    }
    if(POWER_BUS_READ(tx_abrt_source))
        goto failed;
    *value = result;
    return 1U;

failed:
    failed(stage, reg);
    return 0U;
}

uint8_t hal_power_write_register(uint8_t reg, uint8_t value)
{
    uint64_t deadline;
    if(!s_bus_ready) hal_power_bus_init();
    if(!s_bus_ready) return 0U;
    deadline = hal_time_us() + POWER_BUS_TIMEOUT_US;
    POWER_BUS_WRITE(enable, I2C_ENABLE_ENABLE);
    (void)POWER_BUS_READ(clr_tx_abrt);
    POWER_BUS_WRITE(data_cmd, reg);
    POWER_BUS_WRITE(data_cmd, value);
    while((POWER_BUS_READ(status) & I2C_STATUS_ACTIVITY) ||
          !(POWER_BUS_READ(status) & I2C_STATUS_TFE))
    {
        if(POWER_BUS_READ(tx_abrt_source) || hal_time_us() >= deadline)
            goto failed;
    }
    if(POWER_BUS_READ(tx_abrt_source))
        goto failed;
    return 1U;
failed:
    failed(HAL_POWER_TX_IDLE, reg);
    return 0U;
}
