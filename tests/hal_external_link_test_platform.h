#ifndef HK_HAL_EXTERNAL_LINK_TEST_PLATFORM_H
#define HK_HAL_EXTERNAL_LINK_TEST_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#define UART_DEVICE_1 0
#define UART_BITWIDTH_8BIT 8
#define UART_STOP_1 1
#define UART_PARITY_NONE 0
#define DMAC_CHANNEL1 1
#define DMAC_CHANNEL2 2
#define DMAC_ADDR_INCREMENT 0
#define DMAC_ADDR_NOCHANGE 1
#define DMAC_MSIZE_1 0
#define DMAC_TRANS_WIDTH_32 2
#define SYSCTL_DMA_SELECT_UART1_TX_REQ 0
#define SYSCTL_DMA_SELECT_I2C0_RX_REQ 1
#define SYSCTL_DMA_SELECT_UART1_RX_REQ 2
#define SYSCTL_CLOCK_I2C0 0
#define SYSCTL_THRESHOLD_I2C0 0
#define I2C_DEVICE_0 0
#define IRQN_I2C0_INTERRUPT 1
#define I2C_INTR_STAT_RX_OVER 0x02U
#define I2C_INTR_STAT_RX_FULL 0x04U
#define I2C_INTR_STAT_TX_EMPTY 0x10U
#define I2C_INTR_STAT_TX_ABRT 0x40U
#define I2C_INTR_STAT_RD_REQ 0x20U
#define I2C_INTR_STAT_STOP_DET 0x200U
#define I2C_INTR_STAT_START_DET 0x400U
#define I2C_INTR_MASK_RX_OVER I2C_INTR_STAT_RX_OVER
#define I2C_INTR_MASK_RX_FULL I2C_INTR_STAT_RX_FULL
#define I2C_INTR_MASK_TX_EMPTY I2C_INTR_STAT_TX_EMPTY
#define I2C_INTR_MASK_TX_ABRT I2C_INTR_STAT_TX_ABRT
#define I2C_INTR_MASK_RD_REQ I2C_INTR_STAT_RD_REQ
#define I2C_INTR_MASK_STOP_DET I2C_INTR_STAT_STOP_DET
#define I2C_INTR_MASK_START_DET I2C_INTR_STAT_START_DET
#define I2C_RXFLR_VALUE_MASK 0xffU
#define I2C_TXFLR_VALUE_MASK 0xffU
#define I2C_RX_TL_VALUE(value) ((uint32_t)(value))
#define I2C_TX_TL_VALUE(value) ((uint32_t)(value))
#define I2C_STATUS_ACTIVITY 0x01U
#define I2C_STATUS_TFNF 0x02U
#define I2C_STATUS_TFE 0x04U
#define I2C_DATA_CMD_DATA(value) ((uint32_t)(value))
#define I2C_DATA_CMD_CMD 0x100U
#define I2C_DMA_CR_RDMAE 1U
#define I2C_ENABLE_ENABLE 1U
#define I2C_CON_SPEED(x) ((x) << 1U)
#define I2C_CON_STOP_DET_IFADDRESSED 0x80U
#define I2C_SAR_ADDRESS(x) (x)
#define I2C_SS_SCL_HCNT_COUNT(x) (x)
#define I2C_SS_SCL_LCNT_COUNT(x) (x)

typedef struct
{
    volatile uint32_t THR;
    volatile uint32_t RBR;
    volatile uint32_t TFL;
    volatile uint32_t LSR;
} uart_t;

typedef int sysctl_dma_channel_t;
typedef int dmac_channel_number_t;

typedef struct
{
    volatile uint64_t chen;
} dmac_t;

typedef struct
{
    volatile uint32_t intr_stat;
    volatile uint32_t raw_intr_stat;
    volatile uint32_t rxflr;
    volatile uint32_t data_cmd;
    volatile uint32_t intr_mask;
    volatile uint32_t rx_tl;
    volatile uint32_t tx_tl;
    volatile uint32_t enable;
    volatile uint32_t clr_intr;
    volatile uint32_t clr_rx_over;
    volatile uint32_t clr_tx_abrt;
    volatile uint32_t clr_stop_det;
    volatile uint32_t tx_abrt_source;
    volatile uint32_t status;
    volatile uint32_t txflr;
    volatile uint32_t con;
    volatile uint32_t sar;
    volatile uint32_t ss_scl_hcnt;
    volatile uint32_t ss_scl_lcnt;
    volatile uint32_t dma_cr;
    volatile uint32_t dma_rdlr;
    volatile uint32_t clr_start_det;
    volatile uint32_t clr_rd_req;
} i2c_t;

typedef int (*plic_irq_callback_t)(void *context);

extern volatile uart_t *uart[1];
extern volatile i2c_t *i2c[1];
extern volatile dmac_t *dmac;

void board_external_link_uart_pins(void);
void board_external_link_i2c_pins(void);
void uart_init(int device);
void uart_configure(int device, uint32_t baud, int width,
                    int stop, int parity);
int dmac_is_idle(int channel);
void dmac_channel_disable(int channel);
void dmac_set_single_mode(int channel, const void *source, void *dest,
                          int source_increment, int dest_increment,
                          int burst_size, int width, size_t count);
void sysctl_dma_select(sysctl_dma_channel_t channel, int source);
void sysctl_clock_enable(int clock);
void sysctl_clock_set_threshold(int clock, uint32_t divisor);
void i2c_init(int device, uint32_t address, uint32_t address_width,
              uint32_t frequency);
int plic_irq_disable(int interrupt);
int plic_irq_enable(int interrupt);
int plic_set_priority(int interrupt, uint32_t priority);
void plic_irq_register(int interrupt,
                       plic_irq_callback_t callback, void *context);
void plic_irq_unregister(int interrupt);
void hal_external_i2c_test_write_command(uint32_t command);
uint32_t hal_external_i2c_test_read_data(void);

#endif
