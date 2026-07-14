#include "board_hackylens.h"

#include "board_pins.h"

#include <stdio.h>

#include <fpioa.h>

#include "../hal/hal_gpio.h"
#include "../hal/hal_pwm.h"
#include "../hal/hal_spi.h"

void board_leds_init_candidate(void)
{
    fpioa_set_function(IO_LED_ILLUM, FUNC_TIMER2_TOGGLE4);
    fpioa_set_function(IO_RGB_PWM0, FUNC_TIMER2_TOGGLE1);
    fpioa_set_function(IO_RGB_PWM1, FUNC_TIMER2_TOGGLE2);
    fpioa_set_function(IO_RGB_PWM2, FUNC_TIMER2_TOGGLE3);

    fpioa_set_io_driving(IO_LED_ILLUM, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_RGB_PWM0, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_RGB_PWM1, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_RGB_PWM2, FPIOA_DRIVING_15);

    hal_pwm_init(LED_PWM_DEVICE);
}

void board_lcd_init_original(void)
{
    fpioa_set_function(IO_LCD_BL, FUNC_TIMER0_TOGGLE1);
    fpioa_set_function(IO_LCD_DC_OR_AUX, FUNC_GPIOHS15);
    fpioa_set_function(IO_LCD_CS, FUNC_SPI0_SS3);
    fpioa_set_function(IO_LCD_SCLK, FUNC_SPI0_SCLK);
    fpioa_set_function(IO_LCD_RST, FUNC_GPIOHS14);
    fpioa_set_function(IO_LCD_MOSI, FUNC_SPI0_D0);

    fpioa_set_io_driving(IO_LCD_BL, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_LCD_DC_OR_AUX, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_LCD_CS, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_LCD_SCLK, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_LCD_RST, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_LCD_MOSI, FPIOA_DRIVING_15);

    hal_gpiohs_config_output(GPIOHS_LCD_DC_OR_AUX);
    hal_gpiohs_config_output(GPIOHS_LCD_RST);
    hal_gpiohs_write(GPIOHS_LCD_DC_OR_AUX, 0);

    hal_pwm_init(SCREEN_BL_PWM_DEVICE);

    hal_spi0_enable_dvp_data();
    hal_spi_init(LCD_SPI, 32);
    uint32_t actual = hal_spi_set_clock(LCD_SPI, 40000000);
    printf("[LCD] spi original-ish clk=%u\r\n", actual);
}

void board_buttons_init(void)
{
    fpioa_set_function(IO_BTN_LEFT, FUNC_GPIOHS2);
    fpioa_set_function(IO_BTN_OK, FUNC_GPIOHS3);
    fpioa_set_function(IO_BTN_RIGHT, FUNC_GPIOHS4);
    fpioa_set_function(IO_BTN_BACK, FUNC_GPIOHS5);

    fpioa_set_io_pull(IO_BTN_LEFT, FPIOA_PULL_UP);
    fpioa_set_io_pull(IO_BTN_OK, FPIOA_PULL_UP);
    fpioa_set_io_pull(IO_BTN_RIGHT, FPIOA_PULL_UP);
    fpioa_set_io_pull(IO_BTN_BACK, FPIOA_PULL_UP);

    hal_gpiohs_config_input_pull_up(GPIOHS_BTN_LEFT);
    hal_gpiohs_config_input_pull_up(GPIOHS_BTN_OK);
    hal_gpiohs_config_input_pull_up(GPIOHS_BTN_RIGHT);
    hal_gpiohs_config_input_pull_up(GPIOHS_BTN_BACK);
}

void board_sd_spi_init_pins(void)
{
    fpioa_set_function(IO_SD_SCLK, FUNC_SPI1_SCLK);
    fpioa_set_function(IO_SD_D0, FUNC_SPI1_D0);
    fpioa_set_function(IO_SD_D1, FUNC_SPI1_D1);
    fpioa_set_function(IO_SD_CS, FUNC_GPIOHS6);

    fpioa_set_io_driving(IO_SD_SCLK, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_SD_D0, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_SD_CS, FPIOA_DRIVING_15);

    hal_gpiohs_config_output(GPIOHS_SD_CS);
}

void board_camera_init_pins(void)
{
    fpioa_set_function(IO_CAM_PCLK, FUNC_CMOS_PCLK);
    fpioa_set_function(IO_CAM_XCLK, FUNC_CMOS_XCLK);
    fpioa_set_function(IO_CAM_HREF, FUNC_CMOS_HREF);
    fpioa_set_function(IO_CAM_PWDN, FUNC_CMOS_PWDN);
    fpioa_set_function(IO_CAM_VSYNC, FUNC_CMOS_VSYNC);
    fpioa_set_function(IO_CAM_RST, FUNC_CMOS_RST);
    fpioa_set_function(IO_CAM_SCCB_SCLK, FUNC_SCCB_SCLK);
    fpioa_set_function(IO_CAM_SCCB_SDA, FUNC_SCCB_SDA);

    fpioa_set_io_driving(IO_CAM_XCLK, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_CAM_RST, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_CAM_PWDN, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_CAM_SCCB_SCLK, FPIOA_DRIVING_15);
    fpioa_set_io_driving(IO_CAM_SCCB_SDA, FPIOA_DRIVING_15);

    hal_spi0_enable_dvp_data();
}
