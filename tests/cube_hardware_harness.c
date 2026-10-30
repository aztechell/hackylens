#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "camera_sensor.h"
#include "hk_board_port.h"
#include "hk_lights.h"
#include "lcd_st7789_transport.h"

static uint8_t registers[256];
static uint8_t written[256];
static uint32_t writes;
static uint32_t prepares;
static uint64_t now_us;
static double duty[3][4];
static uint8_t pwm_enabled[3][4];
static uint8_t last_command;
static uint8_t madctl;
static uint8_t pixel_format;
static uint32_t phase_bits;
static uint8_t phase_command;
static uint8_t streaming;
static uint8_t aborted;

static void prepare_camera(void) { ++prepares; }
const hk_board_ops_t hk_board_ops = {.camera_prepare = prepare_camera};
uint64_t hal_time_us(void) { return now_us; }
void hal_sleep_ms(uint32_t ms) { now_us += (uint64_t)ms * 1000U; }
void hal_dvp_init_bus(uint8_t divider) { assert(divider == 8U); }
uint32_t hal_dvp_set_xclk_rate(uint32_t hz) { return hz; }
hk_result_t hal_camera_bus_init(hal_camera_bus_info_t *info, uint64_t deadline)
{ assert(deadline > now_us); *info = (hal_camera_bus_info_t){100000, 0x21, 0, 0}; return HK_OK; }
hk_result_t hal_camera_bus_write8(uint8_t reg, uint8_t value, uint64_t deadline)
{
    assert(deadline > now_us);
    registers[reg] = value; written[reg] = 1U; ++writes; return HK_OK;
}
hk_result_t hal_camera_bus_read8(uint8_t reg, uint8_t *out, uint64_t deadline)
{ assert(deadline > now_us); *out = registers[reg]; return HK_OK; }
void hal_pwm_init(uint8_t device) { assert(device < 3U); }
void hal_pwm_set(uint8_t device, uint8_t channel, double frequency, double value)
{
    assert(frequency == PWM_FREQ_HZ && device < 3U && channel < 4U);
    assert(value >= 0.0 && value <= 1.0);
    duty[device][channel] = value;
}
void hal_pwm_enable(uint8_t device, uint8_t channel, uint8_t enabled)
{
    pwm_enabled[device][channel] = enabled;
}
void hal_gpiohs_write(uint8_t pin, uint8_t high) { (void)pin; (void)high; }
void hal_spi_parallel8_phase(uint8_t device, uint32_t bits, uint8_t command)
{
    assert(device == LCD_SPI);
    assert(!streaming);
    phase_bits = bits;
    phase_command = command;
}
void hal_spi_init(uint8_t device, uint32_t bits) { (void)device; phase_bits = bits; }
void hal_spi_fifo_set_frame_bits(uint8_t device, uint32_t bits)
{
    (void)device;
    phase_bits = bits;
}
uint32_t hal_spi_set_clock(uint8_t device, uint32_t hz) { (void)device; return hz; }
void hal_spi_fifo_set_tmod_tx(uint8_t device) { (void)device; }
uint8_t hal_spi_fifo_send_bytes_until(uint8_t device, uint8_t cs,
    const uint8_t *data, size_t size, uint64_t deadline)
{
    assert(device == LCD_SPI && cs == LCD_CS && deadline > now_us);
    assert(phase_bits == 8U && size > 0U);
    if(phase_command)
    {
        assert(size == 1U);
        last_command = data[0];
    }
    else if(last_command == 0x36U)
        madctl = data[0];
    else if(last_command == 0x3AU)
        pixel_format = data[0];
    return 1U;
}
uint8_t hal_spi_fifo_tx_begin_until(uint8_t device, uint8_t cs, uint64_t deadline)
{
    assert(device == LCD_SPI && cs == LCD_CS && deadline > now_us);
    assert(phase_bits == 16U && !phase_command && !streaming);
    streaming = 1U;
    return 1U;
}
uint8_t hal_spi_fifo_tx_write_u16be_until(uint8_t device,
    const uint8_t *pixels, size_t bytes, uint64_t deadline)
{
    assert(device == LCD_SPI && pixels && bytes % 2U == 0U);
    assert(deadline > now_us && streaming);
    return 1U;
}
uint8_t hal_spi_fifo_tx_end_until(uint8_t device, uint64_t deadline)
{
    assert(device == LCD_SPI && deadline > now_us && streaming);
    streaming = 0U;
    return 1U;
}
void hal_spi_fifo_tx_abort(uint8_t device)
{
    assert(device == LCD_SPI);
    streaming = 0U;
    aborted = 1U;
}

int main(void)
{
    uint16_t mid;
    uint16_t pid;
    uint32_t xclk;
    uint32_t sccb;
    uint32_t before;
    const uint8_t pixels[] = {0xF8U, 0x00U, 0x07U, 0xE0U};
    hk_display_rect_t rect = {0, 0, 2U, 1U};
    hk_deadline_t deadline;

    registers[0x1CU] = 0x7FU;
    registers[0x1DU] = 0xA2U;
    registers[0x0AU] = 0x77U;
    registers[0x0BU] = 0x42U;
    camera_sensor_init_bus(&xclk, &sccb);
    assert(prepares == 1U && xclk == 22000000U && sccb == CAMERA_CONTROL_CLOCK_HZ);
    camera_sensor_probe_id(&mid, &pid);
    assert(camera_sensor_matches_id(mid, pid));
    assert(!camera_sensor_matches_id(mid, 0x2642U));
    assert(!camera_sensor_matches_id(0xFFFFU, pid));
    assert(strcmp(camera_sensor_name(), "OV7740") == 0);
    before = writes;
    camera_sensor_select_bank(1U);
    assert(writes == before && !written[0xFFU]);
    camera_sensor_apply_init_table();
    assert(writes > 60U && written[0x12U] && registers[0x31U] == 0x50U);
    camera_sensor_apply_common_tuning();
    assert((registers[0x0CU] & 0xC0U) == 0xC0U);
    camera_sensor_apply_output_size(640U, 480U);
    assert(registers[0x31U] == 0xA0U && registers[0x32U] == 0xF0U);
    camera_sensor_apply_output_size(160U, 120U);
    assert(registers[0x31U] == 0x28U && registers[0x32U] == 0x3CU);
    camera_sensor_apply_output_size(320U, 240U);
    assert(registers[0x31U] == 0x50U && registers[0x32U] == 0x78U);
    camera_sensor_apply_colorbar(1U);
    assert(registers[0x84U] == 0x02U);
    camera_sensor_apply_colorbar(0U);
    assert(registers[0x84U] == 0U);

    lights_screen_backlight_set(25U);
    assert(fabs(duty[SCREEN_BL_PWM_DEVICE][SCREEN_BL_PWM_CHANNEL] - 0.75) < 0.0001);
    lights_screen_backlight_off();
    assert(duty[SCREEN_BL_PWM_DEVICE][SCREEN_BL_PWM_CHANNEL] == 1.0);
    assert(pwm_enabled[SCREEN_BL_PWM_DEVICE][SCREEN_BL_PWM_CHANNEL]);
    lights_rgb_set(1U, 100U, 50U, 0U);
    assert(duty[RGB_PWM_DEVICE][RGB_PWM_CHANNEL0] == 0.0);
    assert(duty[RGB_PWM_DEVICE][RGB_PWM_CHANNEL1] == 0.5);
    assert(duty[RGB_PWM_DEVICE][RGB_PWM_CHANNEL2] == 1.0);
    lights_rgb_set(0U, 100U, 100U, 100U);
    assert(duty[RGB_PWM_DEVICE][RGB_PWM_CHANNEL0] == 1.0);
    assert(pwm_enabled[RGB_PWM_DEVICE][RGB_PWM_CHANNEL0]);
    lights_illum_set(1U, 50U);
    assert(duty[LED_PWM_DEVICE][LED_PWM_CHANNEL] == 0.5);
    lights_illum_set(0U, 50U);
    assert(pwm_enabled[LED_PWM_DEVICE][LED_PWM_CHANNEL]);
    assert(duty[LED_PWM_DEVICE][LED_PWM_CHANNEL] == 1.0);
    lights_driver_prepare();
    assert(duty[LED_PWM_DEVICE][LED_PWM_CHANNEL] == 1.0);
    assert(duty[RGB_PWM_DEVICE][RGB_PWM_CHANNEL0] == 1.0);

    lcd_st7789_transport_prepare();
    lcd_st7789_transport_init();
    assert(madctl == 0x60U && pixel_format == 0x55U);
    assert(lcd_st7789_transport_shadow_size() == 240U * 240U * 2U);
    deadline.at_us = now_us + 100000U;
    assert(lcd_st7789_transport_begin(&rect, deadline) == HK_OK);
    assert(lcd_st7789_transport_write(pixels, sizeof(pixels), deadline, NULL) == HK_OK);
    assert(lcd_st7789_transport_end(deadline, NULL) == HK_OK);
    assert(!streaming);
    assert(lcd_st7789_transport_begin(&rect, deadline) == HK_OK);
    now_us = deadline.at_us;
    assert(lcd_st7789_transport_write(pixels, sizeof(pixels), deadline, NULL) == HK_ERR_DEADLINE_EXCEEDED);
    assert(aborted && !streaming);
    puts("CUBE_HARDWARE_OK sensor-id sizes flat-registers octal-phases inverse-lighting deadline-abort");
    return 0;
}
