#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <i2c.h>
static i2c_t mmio;
static uint64_t now;
static unsigned stops, commands, ticks, latency = 2;
static uint8_t byte, pending, reading, stuck_fifo, stuck_tx, abort_next;
static uint32_t values[8];
uint64_t hal_time_us(void) { return now++; }
void i2c_init(i2c_device_number_t controller, uint32_t address, uint32_t bits, uint32_t hz)
{
    assert(controller == 2 && address == 0x21 && bits == 7 && hz == 100000);
    memset(&mmio, 0, sizeof(mmio));
    mmio.tar = address; mmio.status = I2C_STATUS_TFE | I2C_STATUS_TFNF;
    if(stuck_fifo) mmio.rxflr = 1;
    pending = reading = 0;
}
static uint32_t read_mmio(volatile uint32_t *reg)
{
    if(reg == &mmio.clr_tx_abrt) { mmio.tx_abrt_source = 0; return 0; }
    if(reg == &mmio.clr_stop_det) { mmio.raw_intr_stat = 0; return 0; }
    if(pending && ++ticks >= latency) {
        if(abort_next) mmio.tx_abrt_source = 1;
        else { mmio.status = I2C_STATUS_TFE | I2C_STATUS_TFNF; mmio.raw_intr_stat |= I2C_INTR_STAT_STOP_DET; if(reading) mmio.rxflr = 1; }
        pending = 0;
    }
    if(reg == &mmio.status && stuck_tx) return I2C_STATUS_TFE;
    if(reg == &mmio.data_cmd) { if(!stuck_fifo) mmio.rxflr = 0; return byte; }
    return *reg;
}
static void write_mmio(volatile uint32_t *reg, uint32_t value)
{
    *reg = value;
    if(reg == &mmio.data_cmd) {
        values[commands++ % 8] = value;
        if(value & I2C_DATA_CMD_CMD) reading = 1;
        if(value & (1U << 9)) { ++stops; pending = 1; ticks = 0; mmio.status = I2C_STATUS_ACTIVITY | I2C_STATUS_TFNF; }
    }
}
#define CAMERA_BUS_READ(reg) read_mmio(&mmio.reg)
#define CAMERA_BUS_WRITE(reg, value) write_mmio(&mmio.reg, value)
#include "../platforms/k210/hal/hal_camera_bus_i2c.c"
int main(void)
{
    hal_camera_bus_info_t info;
    assert(hal_camera_bus_init(&info, now+100) == HK_OK);
    assert(info.address_7bit == 0x21 && info.configured_hz == 100000 && info.scl_low_count == 0);
    assert(hal_camera_bus_write8(0x12, 0x80, now+100) == HK_OK);
    assert(stops == 1 && commands == 2 && values[0] == 0x12 && values[1] == 0x280 && mmio.tar == 0x21);
    for(unsigned value = 0; value <= 255; value += 255) {
        uint8_t out = 0x5A; byte = (uint8_t)value; stops = commands = 0;
        assert(hal_camera_bus_read8(0x0A, &out, now+100) == HK_OK);
        assert(out == value && stops == 2 && commands == 2);
        assert(values[0] == 0x20A && values[1] == 0x300);
    }
    uint8_t out = 0xA5; abort_next = 1;
    assert(hal_camera_bus_read8(1, &out, now+100) == HK_ERR_IO);
    assert(out == 0xA5 && mmio.enable == 0);
    assert(hal_camera_bus_write8(1, 2, now+100) == HK_ERR_INVALID_STATE);
    abort_next = 0; stuck_fifo = 1;
    assert(hal_camera_bus_init(&info, now+5) == HK_ERR_DEADLINE_EXCEEDED);
    stuck_fifo = 0;
    assert(hal_camera_bus_init(&info, now+100) == HK_OK);
    stuck_tx = 1;
    assert(hal_camera_bus_write8(1, 2, now+5) == HK_ERR_DEADLINE_EXCEEDED);
    stuck_tx = 0;
    assert(hal_camera_bus_init(&info, now+100) == HK_OK);
    latency = 7; stops = 0;
    uint64_t deadline = now + 7;
    assert(hal_camera_bus_read8(1, &out, deadline) == HK_ERR_DEADLINE_EXCEEDED);
    assert(out == 0xA5 && now <= deadline+2 && stops == 2);
    assert(hal_camera_bus_read8(1, NULL, now+100) == HK_ERR_INVALID_ARGUMENT);
    puts("CAMERA_BUS_I2C_OK");
}
