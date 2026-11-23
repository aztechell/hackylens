#include <stdint.h>
#include <eagle_soc.h>
#include <uart_register.h>

extern void ets_delay_us(uint32_t us);
extern void ets_wdt_disable(void);

#define WIRE_RX (1U << 4)
#define WIRE_TX (1U << 5)
#define CPU2X_ADDRESS 0x3ff00014U

static uint32_t cycles(void)
{
    uint32_t now;
    __asm__ volatile("rsr %0, ccount" : "=a"(now));
    return now;
}

static void wait_until(uint32_t target)
{
    while((int32_t)(cycles() - target) < 0)
    {
    }
}

static void console_put(uint8_t byte)
{
    while(((READ_PERI_REG(UART_STATUS(0)) >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT) >= 120U)
    {
    }
    WRITE_PERI_REG(UART_FIFO(0), byte);
}

static uint8_t console_read(void)
{
    while(!(READ_PERI_REG(UART_STATUS(0)) & UART_RXFIFO_CNT))
    {
    }
    return READ_PERI_REG(UART_FIFO(0));
}

static void console_text(const char *text)
{
    while(*text)
        console_put(*text++);
}

static void console_hex8(uint8_t value)
{
    static const char digits[] = "0123456789abcdef";
    console_put(digits[value >> 4]);
    console_put(digits[value & 15U]);
}

static void console_hex32(uint32_t value)
{
    for(unsigned shift = 32U; shift; shift -= 8U)
        console_hex8(value >> (shift - 8U));
}

static void wire_write(uint8_t byte, uint32_t bit_cycles)
{
    uint32_t edge = cycles();
    GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, WIRE_TX);
    for(unsigned bit = 0; bit < 8U; ++bit)
    {
        edge += bit_cycles;
        wait_until(edge);
        if(byte & (1U << bit))
            GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, WIRE_TX);
        else
            GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, WIRE_TX);
    }
    wait_until(edge + bit_cycles);
    GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, WIRE_TX);
    wait_until(edge + 2U * bit_cycles);
}

static int wire_read(uint8_t *byte, uint32_t bit_cycles, uint32_t timeout)
{
    uint32_t start = cycles();
    while(GPIO_REG_READ(GPIO_IN_ADDRESS) & WIRE_RX)
    {
        if(cycles() - start >= timeout)
            return 0;
    }
    uint32_t sample = cycles() + bit_cycles + bit_cycles / 2U;
    uint8_t value = 0;
    for(unsigned bit = 0; bit < 8U; ++bit)
    {
        wait_until(sample);
        if(GPIO_REG_READ(GPIO_IN_ADDRESS) & WIRE_RX)
            value |= 1U << bit;
        sample += bit_cycles;
    }
    wait_until(sample);
    *byte = value;
    return (GPIO_REG_READ(GPIO_IN_ADDRESS) & WIRE_RX) ? 1 : -1;
}

static uint16_t payload_crc(const uint8_t *data)
{
    uint16_t crc = 0xffffU;
    for(unsigned i = 0; i < 4U; ++i)
    {
        crc ^= (uint16_t)data[i] << 8;
        for(unsigned bit = 0; bit < 8U; ++bit)
            crc = (crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U);
    }
    return crc;
}

static void transmit(uint16_t count, uint32_t cycles_per_ms, uint32_t bit_cycles)
{
    console_text("UL TX START\r\n");
    uint32_t next = cycles() + 20U * cycles_per_ms;
    uint64_t elapsed = 0;
    const uint32_t period = 20U * cycles_per_ms;
    int value = -1000;
    for(unsigned seq = 0; seq < count; ++seq)
    {
        uint8_t packet[8] = {0xa5U, 0x5aU, seq, seq >> 8, value, (uint16_t)value >> 8, 0, 0};
        uint16_t crc = payload_crc(packet + 2);
        packet[6] = crc;
        packet[7] = crc >> 8;
        wait_until(next);
        for(unsigned i = 0; i < sizeof(packet); ++i)
            wire_write(packet[i], bit_cycles);
        next += period;
        elapsed += period;
        value = value == 1000 ? -1000 : value + 10;
    }
    elapsed = elapsed - period + (cycles() - (next - period));
    console_text("UL TX DONE ");
    console_hex32(count);
    console_put(' ');
    console_hex32(elapsed >> 32);
    console_hex32(elapsed);
    console_text("\r\n");
}

static void exchange(uint32_t cycles_per_ms, uint32_t bit_cycles)
{
    unsigned size = console_read();
    uint8_t packet[256];
    for(unsigned i = 0; i < size; ++i)
        packet[i] = console_read();
    for(unsigned i = 0; i < size; ++i)
        wire_write(packet[i], bit_cycles);

    unsigned received = 0, expected = sizeof(packet);
    int status = 0;
    uint32_t timeout = 900U * cycles_per_ms;
    while(received < expected)
    {
        status = wire_read(packet + received, bit_cycles, timeout);
        if(status <= 0)
            break;
        ++received;
        if(received == 8U && packet[0] == 'H' && packet[1] == 'K' && packet[7] == 0U)
            expected = 10U + packet[6] < sizeof(packet) ? 10U + packet[6] : sizeof(packet);
        timeout = 200U * cycles_per_ms;
    }
    console_text("UL WIRE ");
    console_hex8(status < 0);
    console_put(' ');
    for(unsigned i = 0; i < received; ++i)
        console_hex8(packet[i]);
    console_text("\r\n");
}

void probe_entry(void)
{
    ets_wdt_disable();
    unsigned processor_status;
    __asm__ volatile("rsil %0, 15" : "=a"(processor_status) :: "memory");
    SET_PERI_REG_MASK(CPU2X_ADDRESS, 1U);
    GPIO_REG_WRITE(GPIO_ENABLE_W1TC_ADDRESS, WIRE_RX | WIRE_TX);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
    PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO4_U);
    GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, WIRE_TX);
    GPIO_REG_WRITE(GPIO_ENABLE_W1TS_ADDRESS, WIRE_TX);

    uint32_t before = cycles();
    ets_delay_us(5000U);
    uint32_t cycles_per_ms = (cycles() - before) / 5U;
    uint32_t bit_cycles = (cycles_per_ms * 1000U + 57600U) / 115200U;
    for(;;)
    {
        uint8_t command = console_read();
        if(command == 'I')
        {
            console_text("UL TX CLOCK ");
            console_hex32(cycles_per_ms);
            console_text("\r\n");
        }
        if(command == 'C')
        {
            console_text("UL CLOCK START\r\n");
            wait_until(cycles() + 320000000U);
            console_text("UL CLOCK END\r\n");
        }
        if(command == 'S')
        {
            uint32_t calibration = 0;
            for(unsigned shift = 0; shift < 32U; shift += 8U)
                calibration |= (uint32_t)console_read() << shift;
            if(calibration >= 10000U && calibration <= 200000U)
            {
                cycles_per_ms = calibration;
                bit_cycles = (cycles_per_ms * 1000U + 57600U) / 115200U;
                console_text("UL CLOCK SET\r\n");
            }
        }
        if(command == 'T')
        {
            uint16_t count = console_read();
            count |= (uint16_t)console_read() << 8;
            if(count && count <= 20000U)
                transmit(count, cycles_per_ms, bit_cycles);
        }
        if(command == 'U')
            exchange(cycles_per_ms, bit_cycles);
    }
}
