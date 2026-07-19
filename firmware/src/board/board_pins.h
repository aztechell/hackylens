#ifndef HK_BOARD_PINS_H
#define HK_BOARD_PINS_H

#define IO_LCD_DC_OR_AUX 18
#define IO_LCD_CS 19
#define IO_LCD_SCLK 20
#define IO_LCD_MOSI 21
#define IO_LCD_RST 22
#define IO_LCD_BL 24
#define IO_LED_ILLUM 23
#define IO_RGB_PWM0 32
#define IO_RGB_PWM1 30
#define IO_RGB_PWM2 31

#define GPIOHS_LCD_RST 14
#define GPIOHS_LCD_DC_OR_AUX 15

#define IO_BTN_LEFT 39
#define IO_BTN_OK 38
#define IO_BTN_RIGHT 37
#define IO_BTN_BACK 36

#define IO_CAM_PCLK 47
#define IO_CAM_XCLK 46
#define IO_CAM_HREF 45
#define IO_CAM_PWDN 44
#define IO_CAM_VSYNC 43
#define IO_CAM_RST 42
#define IO_CAM_SCCB_SCLK 40
#define IO_CAM_SCCB_SDA 41
#define IO_SD_SCLK 27
#define IO_SD_D0 28
#define IO_SD_D1 26
#define IO_SD_CS 29

/* External four-pin T/R/-/+ connector. */
#define IO_EXTERNAL_UART_R 34
#define IO_EXTERNAL_UART_T 35
#define IO_EXTERNAL_I2C_R 34
#define IO_EXTERNAL_I2C_T 35

#define GPIOHS_BTN_LEFT 2
#define GPIOHS_BTN_OK 3
#define GPIOHS_BTN_RIGHT 4
#define GPIOHS_BTN_BACK 5
#define GPIOHS_SD_CS 6

#define LCD_SPI 0
#define LCD_CS 3
#define GPIOHS_LCD_DC_BIT (1U << GPIOHS_LCD_DC_OR_AUX)
#define SD_SPI 1
#define SD_SPI_CS 3
#define FLASH_SPI 3
#define FLASH_SPI_CS 0

#define LED_PWM_DEVICE 2
#define LED_PWM_CHANNEL 3
#define RGB_PWM_DEVICE 2
#define RGB_PWM_CHANNEL0 0
#define RGB_PWM_CHANNEL1 1
#define RGB_PWM_CHANNEL2 2
#define SCREEN_BL_PWM_DEVICE 0
#define SCREEN_BL_PWM_CHANNEL 0
#define PWM_FREQ_HZ 20000.0

#endif
