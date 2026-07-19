#ifndef BOARD_HACKYLENS_H
#define BOARD_HACKYLENS_H


void board_lcd_init_original(void);
void board_leds_init_candidate(void);
void board_buttons_init(void);
void board_sd_spi_init_pins(void);
void board_camera_init_pins(void);
void board_external_link_uart_pins(void);
void board_external_link_i2c_pins(void);

#endif
