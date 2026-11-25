#include "hal_external_link.h"
#include "hal_time.h"

#include <encoding.h>
#include <fpioa.h>
#include <gpiohs.h>
#include <platform.h>

#include "../../../firmware/src/internal/hk_board_port.h"

#define PROBE_HALF_PERIOD_US 12U
#define PROBE_TIMEOUT_US 2000U

static volatile gpiohs_raw_t *const gpio = (volatile gpiohs_raw_t *)GPIOHS_BASE_ADDR;

static uint8_t hold_line(uint64_t deadline)
{
    uint64_t until = hal_time_us() + PROBE_HALF_PERIOD_US;
    if(until >= deadline)
        return 0U;
    while(hal_time_us() < until)
    {
    }
    return 1U;
}

static uint8_t clock_high(uint32_t scl, uint64_t deadline)
{
    while(!(gpio->input_val & scl))
        if(hal_time_us() >= deadline)
            return 0U;
    return hold_line(deadline);
}

static uint32_t gpiohs_spare(void)
{
    uint32_t used = gpio->rise_ie | gpio->fall_ie | gpio->high_ie | gpio->low_ie;
    for(int pin = 0; pin < FPIOA_NUM_IO; ++pin)
    {
        fpioa_io_config_t cfg;
        if(fpioa_get_io(pin, &cfg) != 0)
            return 0U;
        if(cfg.ch_sel >= FUNC_GPIOHS0 && cfg.ch_sel <= FUNC_GPIOHS31)
            used |= UINT32_C(1) << (cfg.ch_sel - FUNC_GPIOHS0);
    }
    return ~used;
}

static int map_gpio(int pin, unsigned channel, fpioa_io_config_t cfg)
{
    cfg.ch_sel = FUNC_GPIOHS0 + channel;
    cfg.oe_en = 1U;
    cfg.oe_inv = 0U;
    cfg.do_sel = 0U;
    cfg.do_inv = 0U;
    cfg.ie_en = 1U;
    cfg.ie_inv = 0U;
    cfg.di_inv = 0U;
    return fpioa_set_io(pin, &cfg);
}

static hal_i2c_probe_result_t send_address(uint8_t address, uint32_t scl, uint32_t sda)
{
    const uint32_t released = gpio->output_en;
    const uint64_t deadline = hal_time_us() + PROBE_TIMEOUT_US;

    if(!clock_high(scl, deadline))
        return HAL_I2C_PROBE_TIMEOUT;
    if(!(gpio->input_val & sda))
        return HAL_I2C_PROBE_BUSY;
    gpio->output_en = released | sda; /* START */
    if(!hold_line(deadline))
        return HAL_I2C_PROBE_TIMEOUT;

    const uint8_t write_address = address << 1U;
    for(uint8_t bit = 0x80U; bit != 0U; bit >>= 1U)
    {
        const uint32_t sink = (write_address & bit) ? 0U : sda;
        gpio->output_en = released | scl | (gpio->output_en & sda);
        gpio->output_en = released | scl | sink;
        if(!hold_line(deadline))
            return HAL_I2C_PROBE_TIMEOUT;
        gpio->output_en = released | sink;
        if(!clock_high(scl, deadline))
            return HAL_I2C_PROBE_TIMEOUT;
        if(!sink && !(gpio->input_val & sda))
            return HAL_I2C_PROBE_BUSY;
    }

    gpio->output_en = released | scl | sda;
    gpio->output_en = released | scl;
    if(!hold_line(deadline))
        return HAL_I2C_PROBE_TIMEOUT;
    gpio->output_en = released;
    if(!clock_high(scl, deadline))
        return HAL_I2C_PROBE_TIMEOUT;
    const uint8_t ack = (gpio->input_val & sda) == 0U;

    gpio->output_en = released | scl;
    gpio->output_en = released | scl | sda;
    if(!hold_line(deadline))
        return HAL_I2C_PROBE_TIMEOUT;
    gpio->output_en = released | sda;
    if(!clock_high(scl, deadline))
        return HAL_I2C_PROBE_TIMEOUT;
    gpio->output_en = released; /* STOP */
    if(!hold_line(deadline))
        return HAL_I2C_PROBE_TIMEOUT;
    if(!(gpio->input_val & sda))
        return HAL_I2C_PROBE_BUSY;
    return ack ? HAL_I2C_PROBE_ACK : HAL_I2C_PROBE_NACK;
}

hal_i2c_probe_result_t hal_external_i2c_probe(uint8_t address)
{
    fpioa_io_config_t scl_cfg, sda_cfg;
    hal_i2c_probe_result_t result = HAL_I2C_PROBE_UNAVAILABLE;
    hk_board_ops.external_i2c_prepare();
    const int scl_pin = fpioa_get_io_by_function(FUNC_I2C0_SCLK);
    const int sda_pin = fpioa_get_io_by_function(FUNC_I2C0_SDA);
    if(scl_pin < 0 || sda_pin < 0 ||
       fpioa_get_io(scl_pin, &scl_cfg) != 0 || fpioa_get_io(sda_pin, &sda_cfg) != 0)
        return result;

    /* The K210 FIFO starts a transaction with a data command. Borrow two
     * unmapped, interrupt-free GPIOHS channels for the address-only probe. */
    const unsigned long irq = clear_csr(mstatus, MSTATUS_MIE);
    const uint32_t spare = gpiohs_spare();
    if(spare && (spare & (spare - 1U)))
    {
        const unsigned scl_channel = 31U - (unsigned)__builtin_clz(spare);
        const uint32_t scl = UINT32_C(1) << scl_channel;
        const unsigned sda_channel = 31U - (unsigned)__builtin_clz(spare & ~scl);
        const uint32_t sda = UINT32_C(1) << sda_channel;
        const uint32_t mask = scl | sda;
        const uint32_t saved_inputs = gpio->input_en;
        const uint32_t saved_outputs = gpio->output_en;
        const uint32_t saved_level = gpio->output_val;
        const uint32_t saved_invert = gpio->output_xor;
        const uint32_t saved_iof = gpio->iof_en;
        const uint32_t saved_rise = gpio->rise_ip;
        const uint32_t saved_fall = gpio->fall_ip;
        const uint32_t saved_high = gpio->high_ip;
        const uint32_t saved_low = gpio->low_ip;

        /* Drive only LOW. Releasing a line lets its pull-up raise it. */
        gpio->output_en &= ~mask;
        gpio->output_val &= ~mask;
        gpio->output_xor &= ~mask;
        gpio->iof_en &= ~mask;
        gpio->input_en |= mask;
        if(map_gpio(scl_pin, scl_channel, scl_cfg) == 0 &&
           map_gpio(sda_pin, sda_channel, sda_cfg) == 0)
            result = send_address(address, scl, sda);

        gpio->output_en &= ~mask;
        (void)fpioa_set_io(scl_pin, &scl_cfg);
        (void)fpioa_set_io(sda_pin, &sda_cfg);
        gpio->input_en = saved_inputs;
        gpio->output_val = saved_level;
        gpio->output_xor = saved_invert;
        gpio->iof_en = saved_iof;
        gpio->output_en = saved_outputs;
        gpio->rise_ip = gpio->rise_ip & ~saved_rise & mask;
        gpio->fall_ip = gpio->fall_ip & ~saved_fall & mask;
        gpio->high_ip = gpio->high_ip & ~saved_high & mask;
        gpio->low_ip = gpio->low_ip & ~saved_low & mask;
    }
    if(irq & MSTATUS_MIE)
        (void)set_csr(mstatus, MSTATUS_MIE);
    return result;
}
