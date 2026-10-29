#include "hk_board_port.h"

#include <fpioa.h>
#include <sysctl.h>

#include "board_config.h"
#include "hal_gpio.h"
#include "hal_pwm.h"
#include "hal_spi.h"

static void cube_early_init(void)
{
    /* Sipeed MaixPy-v1 startup: the dedicated camera/LCD data banks are 1.8V.
     * Other banks retain their board-selected boot voltage. */
    sysctl_set_power_mode(SYSCTL_POWER_BANK6, SYSCTL_POWER_V18);
    sysctl_set_power_mode(SYSCTL_POWER_BANK7, SYSCTL_POWER_V18);
    fpioa_set_function(IO_PMIC_SCL, IO_PMIC_SCL_FUNCTION);
    fpioa_set_function(IO_PMIC_SDA, IO_PMIC_SDA_FUNCTION);
    fpioa_set_io_pull(IO_PMIC_SCL, FPIOA_PULL_UP);
    fpioa_set_io_pull(IO_PMIC_SDA, FPIOA_PULL_UP);
    hal_spi0_enable_dvp_data();
}

static void cube_pwm_off(uint8_t device, uint8_t channel)
{
    /* All Cube light gates are active-low. Establish the inactive level
     * before attaching the physical pin to its PWM signal. */
    hal_pwm_init(device);
    hal_pwm_set(device, channel, PWM_FREQ_HZ, 1.0);
    hal_pwm_enable(device, channel, 1U);
}

static void cube_lights_prepare(void)
{
    cube_pwm_off(LED_PWM_DEVICE, LED_PWM_CHANNEL);
    cube_pwm_off(RGB_PWM_DEVICE, RGB_PWM_CHANNEL0);
    cube_pwm_off(RGB_PWM_DEVICE, RGB_PWM_CHANNEL1);
    cube_pwm_off(RGB_PWM_DEVICE, RGB_PWM_CHANNEL2);
    fpioa_set_function(IO_LED_ILLUM, IO_LED_ILLUM_FUNCTION);
    fpioa_set_function(IO_RGB_PWM0, IO_RGB_PWM0_FUNCTION);
    fpioa_set_function(IO_RGB_PWM1, IO_RGB_PWM1_FUNCTION);
    fpioa_set_function(IO_RGB_PWM2, IO_RGB_PWM2_FUNCTION);

    fpioa_set_io_driving(IO_LED_ILLUM, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_RGB_PWM0, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_RGB_PWM1, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_RGB_PWM2, FPIOA_DRIVING_15);

}

static void cube_display_prepare(void)
{
    cube_pwm_off(SCREEN_BL_PWM_DEVICE, SCREEN_BL_PWM_CHANNEL);
    fpioa_set_function(IO_LCD_BL, IO_LCD_BL_FUNCTION);
    fpioa_set_function(IO_LCD_DC_OR_AUX, IO_LCD_DC_OR_AUX_FUNCTION);
    fpioa_set_function(IO_LCD_CS, IO_LCD_CS_FUNCTION);
    fpioa_set_function(IO_LCD_SCLK, IO_LCD_SCLK_FUNCTION);
    fpioa_set_function(IO_LCD_RST, IO_LCD_RST_FUNCTION);

    fpioa_set_io_driving(IO_LCD_BL, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_LCD_DC_OR_AUX, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_LCD_CS, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_LCD_SCLK, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_LCD_RST, FPIOA_DRIVING_15);

    hal_gpiohs_config_output(GPIOHS_LCD_DC_OR_AUX);
    hal_gpiohs_config_output(GPIOHS_LCD_RST);
    hal_gpiohs_write(GPIOHS_LCD_DC_OR_AUX, 0);

    /* Cube LCD D0..D7 use the fixed SPI0/DVP octal data bus, not FPIOA MOSI. */
    hal_spi0_enable_dvp_data();
}

static void cube_buttons_prepare(void)
{
    fpioa_set_function(IO_BTN_LEFT, IO_BTN_LEFT_FUNCTION);
    fpioa_set_function(IO_BTN_OK, IO_BTN_OK_FUNCTION);
    fpioa_set_function(IO_BTN_RIGHT, IO_BTN_RIGHT_FUNCTION);

    fpioa_set_io_pull(IO_BTN_LEFT, FPIOA_PULL_UP);
    fpioa_set_io_pull(IO_BTN_OK, FPIOA_PULL_UP);
    fpioa_set_io_pull(IO_BTN_RIGHT, FPIOA_PULL_UP);

    hal_gpiohs_config_input_pull_up(GPIOHS_BTN_LEFT);
    hal_gpiohs_config_input_pull_up(GPIOHS_BTN_OK);
    hal_gpiohs_config_input_pull_up(GPIOHS_BTN_RIGHT);
}

static void cube_sd_prepare(void)
{
    fpioa_set_function(IO_SD_SCLK, IO_SD_SCLK_FUNCTION);
    fpioa_set_function(IO_SD_D0, IO_SD_D0_FUNCTION);
    fpioa_set_function(IO_SD_D1, IO_SD_D1_FUNCTION);
    fpioa_set_function(IO_SD_CS, IO_SD_CS_FUNCTION);

    fpioa_set_io_driving(IO_SD_SCLK, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_SD_D0, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_SD_CS, FPIOA_DRIVING_15);
    hal_gpiohs_config_output(GPIOHS_SD_CS);
}

static void cube_camera_prepare(void)
{
    fpioa_set_function(IO_CAM_PCLK, IO_CAM_PCLK_FUNCTION);
    fpioa_set_function(IO_CAM_XCLK, IO_CAM_XCLK_FUNCTION);
    fpioa_set_function(IO_CAM_HREF, IO_CAM_HREF_FUNCTION);
    fpioa_set_function(IO_CAM_PWDN, IO_CAM_PWDN_FUNCTION);
    fpioa_set_function(IO_CAM_VSYNC, IO_CAM_VSYNC_FUNCTION);
    fpioa_set_function(IO_CAM_RST, IO_CAM_RST_FUNCTION);
    fpioa_set_function(IO_CAM_SCCB_SCLK, IO_CAM_SCCB_SCLK_FUNCTION);
    fpioa_set_function(IO_CAM_SCCB_SDA, IO_CAM_SCCB_SDA_FUNCTION);

    fpioa_set_io_driving(IO_CAM_XCLK, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_CAM_RST, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_CAM_PWDN, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_CAM_SCCB_SCLK, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_CAM_SCCB_SDA, FPIOA_DRIVING_15);
    hal_spi0_enable_dvp_data();
}

static void cube_internal_flash_prepare(void)
{
    /* K210 SPI3 is dedicated to the on-board flash; no FPIOA route is needed. */
}

static void cube_external_uart_prepare(void)
{
    fpioa_set_function(IO_EXTERNAL_UART_R, IO_EXTERNAL_UART_R_FUNCTION);
    fpioa_set_function(IO_EXTERNAL_UART_T, IO_EXTERNAL_UART_T_FUNCTION);
    fpioa_set_io_pull(IO_EXTERNAL_UART_R, FPIOA_PULL_UP);
    fpioa_set_io_driving(IO_EXTERNAL_UART_T, FPIOA_DRIVING_7);
}

static void cube_external_i2c_prepare(void)
{
    fpioa_set_function(IO_EXTERNAL_I2C_R, IO_EXTERNAL_I2C_R_FUNCTION);
    fpioa_set_function(IO_EXTERNAL_I2C_T, IO_EXTERNAL_I2C_T_FUNCTION);
    fpioa_set_io_pull(IO_EXTERNAL_I2C_R, FPIOA_PULL_UP);
    fpioa_set_io_pull(IO_EXTERNAL_I2C_T, FPIOA_PULL_UP);
    fpioa_set_io_driving(IO_EXTERNAL_I2C_R, FPIOA_DRIVING_7);
    fpioa_set_io_driving(IO_EXTERNAL_I2C_T, FPIOA_DRIVING_7);
}

const hk_board_ops_t hk_board_ops = {
    .early_init = cube_early_init,
    .display_prepare = cube_display_prepare,
    .camera_prepare = cube_camera_prepare,
    .buttons_prepare = cube_buttons_prepare,
    .lights_prepare = cube_lights_prepare,
    .internal_flash_prepare = cube_internal_flash_prepare,
    .sd_prepare = cube_sd_prepare,
    .external_uart_prepare = cube_external_uart_prepare,
    .external_i2c_prepare = cube_external_i2c_prepare,
};
