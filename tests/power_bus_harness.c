#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <i2c.h>
#include <fpioa.h>
#include <sysctl.h>
#include <gpiohs.h>
#include "board_config.h"

static i2c_t mmio;
static uint64_t now;
static unsigned inits, commands;
static uint8_t noack, no_rx, stuck_idle, stuck_rx, stuck_disable, stuck_enable;
static uint8_t queued, last_reg;
static uint8_t clock_enabled;
static gpiohs_raw_t gpio_mmio;
static fpioa_io_config_t pins[FPIOA_NUM_IO];
static unsigned long irq_state;
static unsigned clock_pulses, gpio_writes;
static unsigned release_after;
static uint8_t scl_stuck, sda_stuck;
static int fail_map_once;
static uint32_t signal_mask(int pin)
{
    unsigned fn = pins[pin].ch_sel;
    return fn >= FUNC_GPIOHS0 && fn <= FUNC_GPIOHS31 ?
        1UL << (fn - FUNC_GPIOHS0) : 0U;
}
static uint32_t wire_levels(void)
{
    uint32_t level = 0;
    uint32_t scl_mask = signal_mask(IO_PMIC_SCL), sda_mask = signal_mask(IO_PMIC_SDA);
    if(!scl_stuck && !(gpio_mmio.output_en & scl_mask)) level |= scl_mask ? scl_mask : 1UL << 30;
    if(!sda_stuck && clock_pulses >= release_after &&
       !(gpio_mmio.output_en & sda_mask)) level |= sda_mask ? sda_mask : 1UL << 31;
    return level;
}
static uint32_t read_gpio(volatile uint32_t *field)
{
    if(field == &gpio_mmio.input_val) return wire_levels();
    return *field;
}
static void write_gpio(volatile uint32_t *field, uint32_t value)
{
    uint32_t before = wire_levels();
    gpio_writes++;
    if(field == &gpio_mmio.rise_ip || field == &gpio_mmio.fall_ip ||
       field == &gpio_mmio.high_ip || field == &gpio_mmio.low_ip) *field &= ~value;
    else *field = value;
    uint32_t scl_mask = signal_mask(IO_PMIC_SCL), sda_mask = signal_mask(IO_PMIC_SDA);
    uint32_t mask = scl_mask | sda_mask;
    if(scl_mask && sda_mask)
    {
        assert(!(gpio_mmio.output_val & gpio_mmio.output_en & mask));
        uint32_t after = wire_levels();
        if(!(before & scl_mask) && (after & scl_mask) &&
           !(gpio_mmio.output_en & sda_mask)) clock_pulses++;
        after = wire_levels();
        gpio_mmio.rise_ip |= (~before & after) & mask;
        gpio_mmio.fall_ip |= (before & ~after) & mask;
        gpio_mmio.high_ip |= after & mask;
        gpio_mmio.low_ip |= ~after & mask;
    }
}
static unsigned long save_irq(void)
{ unsigned long saved = irq_state; irq_state &= ~8UL; return saved; }
#define POWER_GPIO_READ(field) read_gpio(&gpio_mmio.field)
#define POWER_GPIO_WRITE(field, value) write_gpio(&gpio_mmio.field, value)
#define POWER_IRQ_SAVE() save_irq()
#define POWER_IRQ_RESTORE(state) do { if((state) & 8UL) irq_state |= 8UL; } while(0)
int sysctl_clock_enable(sysctl_clock_t clock)
{ assert(clock == SYSCTL_CLOCK_I2C1); clock_enabled = 1U; return 0; }
uint64_t hal_time_us(void) { return ++now; }
int fpioa_get_io(int pin, fpioa_io_config_t *cfg)
{
    assert(pin >= 0 && pin < FPIOA_NUM_IO);
    *cfg = pins[pin];
    if(pin == IO_PMIC_SCL) cfg->pad_di = !scl_stuck;
    if(pin == IO_PMIC_SDA) cfg->pad_di = !sda_stuck && clock_pulses >= release_after;
    return 0;
}
int fpioa_set_io(int pin, fpioa_io_config_t *cfg)
{
    assert(pin == IO_PMIC_SCL || pin == IO_PMIC_SDA);
    if(fail_map_once == pin) { fail_map_once = -1; return -1; }
    pins[pin] = *cfg;
    return 0;
}
void i2c_init(i2c_device_number_t bus, uint32_t address, uint32_t bits, uint32_t hz)
{
    assert(bus == I2C_DEVICE_1 && address == 0x34U && bits == 7U && hz == 100000U);
    /* The SDK must never reconfigure an incompletely disabled controller. */
    assert(!(mmio.enable_status & I2C_ENABLE_STATUS_IC_ENABLE));
    inits++;
    mmio.enable = 1U;
    mmio.enable_status = stuck_enable ? 0U : 1U;
    mmio.con = 0x63U; mmio.tar = address;
    mmio.ss_scl_hcnt = 250U; mmio.ss_scl_lcnt = 251U;
    mmio.status = I2C_STATUS_TFE | I2C_STATUS_TFNF;
    mmio.tx_abrt_source = 0U; mmio.raw_intr_stat = 0U;
    mmio.rxflr = stuck_rx ? 1U : 0U;
    queued = 0U;
}
static uint32_t read_mmio(volatile uint32_t *reg)
{
    assert(clock_enabled);
    if(reg == &mmio.clr_tx_abrt) { mmio.tx_abrt_source = 0U; queued = 0U; return 0U; }
    if(reg == &mmio.data_cmd) { if(!stuck_rx) mmio.rxflr = 0U; return 0xC1U; }
    return *reg;
}
static void write_mmio(volatile uint32_t *reg, uint32_t value)
{
    assert(clock_enabled);
    *reg = value;
    if(reg == &mmio.enable)
    {
        mmio.enable_status = value ? (stuck_enable ? 0U : 1U) : (stuck_disable ? 1U : 0U);
        if(!value)
        {
            /* Deliberately erase abort evidence on disable: snapshot ordering
             * is observable, rather than only checking log text. */
            mmio.tx_abrt_source = 0U;
            queued = 0U;
        }
    }
    if(reg == &mmio.data_cmd)
    {
        commands++;
        if(!queued++) last_reg = (uint8_t)value;
        if(queued >= 2U)
        {
            mmio.tx_abrt_source = noack ? 1U : 0U;
            mmio.raw_intr_stat = noack ? I2C_INTR_STAT_TX_ABRT : 0U;
            mmio.status = stuck_idle || no_rx ? I2C_STATUS_ACTIVITY : I2C_STATUS_TFE;
            if((value & I2C_DATA_CMD_CMD) && !no_rx && !noack) mmio.rxflr = 1U;
        }
    }
}
#define POWER_BUS_READ(field) read_mmio(&mmio.field)
#define POWER_BUS_WRITE(field, value) write_mmio(&mmio.field, value)
#include "../platforms/k210/hal/hal_power.c"

static void reset(void)
{
    memset(&mmio, 0, sizeof(mmio));
    noack = no_rx = stuck_idle = stuck_rx = stuck_disable = stuck_enable = 0U;
    inits = commands = queued = 0U; now = 0U;
    clock_enabled = 0U;
    s_bus_ready = 0U;
    memset(&s_diagnostic, 0, sizeof(s_diagnostic));
    memset(&gpio_mmio, 0x5A, sizeof(gpio_mmio));
    memset(pins, 0, sizeof(pins));
    pins[IO_PMIC_SCL].ch_sel = IO_PMIC_SCL_FUNCTION;
    pins[IO_PMIC_SDA].ch_sel = IO_PMIC_SDA_FUNCTION;
    pins[IO_PMIC_SCL].pu = pins[IO_PMIC_SDA].pu = 1U;
    pins[IO_PMIC_SCL].ie_en = pins[IO_PMIC_SDA].ie_en = 1U;
    irq_state = 0x88UL;
    release_after = clock_pulses = gpio_writes = 0;
    scl_stuck = sda_stuck = 0U;
    fail_map_once = -1;
}
static void check_restored(const gpiohs_raw_t *saved)
{
    assert(memcmp(saved, &gpio_mmio, sizeof(*saved)) == 0);
    assert(pins[IO_PMIC_SCL].ch_sel == IO_PMIC_SCL_FUNCTION);
    assert(pins[IO_PMIC_SDA].ch_sel == IO_PMIC_SDA_FUNCTION);
    assert(pins[IO_PMIC_SCL].pu && pins[IO_PMIC_SDA].pu);
}
int main(void)
{
    hal_power_diagnostic_t diagnostic;
    uint8_t out = 0xA5U;
    reset();
    hal_power_bus_init();
    noack = 1U;
    assert(!hal_power_read_register(0U, &out) && out == 0xA5U);
    hal_power_get_diagnostic(&diagnostic);
    assert(diagnostic.failures == 1U && diagnostic.stage == HAL_POWER_RX_WAIT);
    assert(diagnostic.abort_source == 1U && !mmio.tx_abrt_source && !mmio.enable);
    assert(diagnostic.enable == 1U && diagnostic.enable_status == 1U);
    assert(diagnostic.raw_intr == I2C_INTR_STAT_TX_ABRT && diagnostic.tar == 0x34U);
    assert(diagnostic.con == 0x63U && diagnostic.hcnt == 250U && diagnostic.lcnt == 251U);
    assert(diagnostic.scl_pad == 1U && diagnostic.sda_pad == 1U);
    assert(diagnostic.scl_function == IO_PMIC_SCL_FUNCTION && diagnostic.sda_function == IO_PMIC_SDA_FUNCTION);
    noack = 0U;
    assert(hal_power_read_register(0U, &out) && out == 0xC1U && inits == 2U);
    assert(hal_power_write_register(0x46U, 3U) && last_reg == 0x46U);

    reset(); no_rx = 1U; out = 0xA5U;
    assert(!hal_power_read_register(0x46U, &out) && out == 0xA5U && now < 30000U);
    assert(s_diagnostic.stage == HAL_POWER_RX_WAIT && !s_diagnostic.abort_source);
    assert(s_diagnostic.reg == 0x46U && !s_bus_ready);
    no_rx = 0U;
    assert(hal_power_read_register(0x46U, &out) && inits == 2U);

    reset(); stuck_idle = 1U;
    assert(!hal_power_read_register(0U, &out) && s_diagnostic.stage == HAL_POWER_RX_IDLE);
    assert(now < 30000U);
    reset(); stuck_idle = 1U;
    assert(!hal_power_write_register(0x46U, 3U) && s_diagnostic.stage == HAL_POWER_TX_IDLE);
    assert(now < 30000U);
    reset(); stuck_rx = 1U;
    assert(!hal_power_read_register(0U, &out) && s_diagnostic.stage == HAL_POWER_RX_DRAIN);
    assert(now < 30000U);

    reset(); mmio.enable_status = 1U; stuck_disable = 1U;
    hal_power_bus_init();
    assert(!inits && s_diagnostic.stage == HAL_POWER_INIT_DISABLE && now < 30000U);
    reset(); stuck_enable = 1U;
    hal_power_bus_init();
    assert(inits == 1U && s_diagnostic.stage == HAL_POWER_INIT_ENABLE && now < 30000U);
    stuck_enable = 0U;
    assert(hal_power_read_register(0U, &out) && inits == 2U);
    for(unsigned masked = 0; masked < 2; masked++)
    {
        reset(); release_after = 3; irq_state = masked ? 0x80UL : 0x88UL;
        gpiohs_raw_t saved = gpio_mmio;
        hal_power_bus_init();
        assert(s_bus_ready && inits == 1 && clock_pulses == 3 && now < 500);
        assert(irq_state == (masked ? 0x80UL : 0x88UL));
        check_restored(&saved);
    }
    reset(); sda_stuck = 1;
    gpiohs_raw_t saved = gpio_mmio;
    hal_power_bus_init();
    assert(!s_bus_ready && !inits && s_diagnostic.stage == HAL_POWER_BUS_CLEAR);
    assert(clock_pulses == 9 && now < 600 && irq_state == 0x88UL);
    check_restored(&saved);
    reset(); scl_stuck = 1; saved = gpio_mmio;
    hal_power_bus_init();
    assert(!s_bus_ready && !inits && now < 600 && irq_state == 0x88UL);
    assert(!clock_pulses && s_diagnostic.stage == HAL_POWER_BUS_CLEAR);
    check_restored(&saved);
    reset(); release_after = 3; pins[7].ch_sel = FUNC_GPIOHS30; pins[8].ch_sel = FUNC_GPIOHS31;
    saved = gpio_mmio;
    hal_power_bus_init();
    assert(s_bus_ready && inits == 1 && clock_pulses == 3);
    assert(pins[7].ch_sel == FUNC_GPIOHS30 && pins[8].ch_sel == FUNC_GPIOHS31);
    check_restored(&saved);
    reset(); release_after = 3; saved = gpio_mmio;
    for(unsigned ch = 0, pin = 0; ch < 32; ch++, pin++)
    {
        if(pin == IO_PMIC_SCL || pin == IO_PMIC_SDA) pin = 32;
        pins[pin].ch_sel = FUNC_GPIOHS0 + ch;
    }
    hal_power_bus_init();
    assert(!inits && !gpio_writes && s_diagnostic.stage == HAL_POWER_GPIO_COLLISION);
    assert(irq_state == 0x88UL);
    check_restored(&saved);
    /* Early boot may map every GPIOHS. Later board setup frees channels;
       the next PMIC request must recover without another processor reset. */
    pins[22].ch_sel = pins[23].ch_sel = FUNC_RESV0;
    assert(hal_power_read_register(0U, &out) && out == 0xC1U);
    assert(inits == 1 && clock_pulses == 3 && irq_state == 0x88UL);
    check_restored(&saved);
    reset(); release_after = 3; fail_map_once = IO_PMIC_SDA; saved = gpio_mmio;
    hal_power_bus_init();
    assert(!inits && s_diagnostic.stage == HAL_POWER_BUS_CLEAR && irq_state == 0x88UL);
    check_restored(&saved);
    puts("POWER_BUS_OK snapshot-before-clear bounded-reinit open-drain bus-clear restored");
    return 0;
}
