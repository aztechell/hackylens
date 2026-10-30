#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "encoding.h"
#include "boot_flash.h"
#include "hal_spi.h"
#include "hk_board_port.h"

unsigned long spi_test_mstatus = 0x1888U;
unsigned long spi_test_mhartid;
static uint64_t now;
static uint8_t fail_tx, fail_rx, wel;
static unsigned busy_polls, stuck_busy, status_reads, program_count;
static uint32_t read_address[8], program_address[8];
static size_t read_length[8], program_length[8];
static unsigned read_count;

static void board_prepare(void) {}
const hk_board_ops_t hk_board_ops = { .internal_flash_prepare = board_prepare };
uint64_t hal_time_us(void)
{
    /* In particular, hundreds of flash-busy polls must remain interruptible. */
    assert(spi_test_mstatus == 0x1888U);
    return now += 1000U;
}
void hal_spi_standard_init(uint8_t device, uint32_t hz)
{ assert(device == 3U && hz == 10000000U); }
static uint32_t address(const uint8_t *cmd)
{ return ((uint32_t)cmd[1] << 16) | ((uint32_t)cmd[2] << 8) | cmd[3]; }
uint8_t hal_spi_standard_send_segments_checked(uint8_t device, uint8_t cs,
    const uint8_t *cmd, size_t cmd_len, const uint8_t *data, size_t len,
    uint32_t timeout)
{
    assert(device == 3U && cs == 0U && cmd && cmd_len && timeout == 2000U);
    assert(spi_test_mstatus == 0x1888U && len <= 256U);
    if(cmd[0] == fail_tx) return 0U;
    if(cmd[0] == 6U)
    {
        assert(cmd_len == 1U && !len);
        wel = 2U;
    }
    else if(cmd[0] == 2U)
    {
        assert(cmd_len == 4U && data && len && program_count < 8U);
        program_address[program_count] = address(cmd);
        program_length[program_count++] = len;
        wel = 0U;
        busy_polls = 3U;
    }
    else
    {
        assert(cmd[0] == 0x20U && cmd_len == 4U && !len);
        wel = 0U;
        busy_polls = 5U;
    }
    return 1U;
}
uint8_t hal_spi_standard_receive_checked(uint8_t device, uint8_t cs,
    const uint8_t *cmd, size_t cmd_len, uint8_t *data, size_t len,
    uint32_t timeout)
{
    assert(device == 3U && cs == 0U && cmd && len && len <= 256U);
    assert(spi_test_mstatus == 0x1888U && timeout == 2000U);
    if(cmd[0] == fail_rx) return 0U;
    switch(cmd[0])
    {
    case 0x9FU:
        assert(cmd_len == 1U && len == 3U);
        data[0] = 0xEFU; data[1] = 0x60U; data[2] = 0x18U;
        break;
    case 5U:
        assert(cmd_len == 1U && len == 1U);
        status_reads++;
        data[0] = wel | ((busy_polls || stuck_busy) ? 1U : 0U);
        if(busy_polls) busy_polls--;
        break;
    case 3U:
        assert(cmd_len == 4U && read_count < 8U);
        read_address[read_count] = address(cmd);
        read_length[read_count++] = len;
        for(size_t i = 0U; i < len; i++) data[i] = (uint8_t)(address(cmd) + i);
        break;
    default: assert(0);
    }
    return 1U;
}
static void reset(void)
{
    fail_tx = fail_rx = wel = 0U;
    busy_polls = stuck_busy = status_reads = program_count = read_count = 0U;
    now = 0U;
    assert(boot_flash_init(10000000U) == BOOT_FLASH_OK);
}
static void faulted(void)
{
    boot_flash_info_t info;
    uint8_t id[3];
    boot_flash_get_info(&info);
    assert(info.faulted);
    /* Confirms the operation released its lock and quarantined further I/O. */
    assert(boot_flash_read_id(id) == BOOT_FLASH_ERROR_FAULTED);
    assert(spi_test_mstatus == 0x1888U);
}
int main(void)
{
    uint8_t data[600];
    reset();
    assert(boot_flash_read(0x123400U, data, sizeof(data)) == BOOT_FLASH_OK);
    assert(read_count == 3U && read_length[0] == 256U && read_length[1] == 256U && read_length[2] == 88U);
    assert(read_address[0] == 0x123400U && read_address[1] == 0x123500U && read_address[2] == 0x123600U);
    for(unsigned i = 0; i < sizeof(data); i++) assert(data[i] == (uint8_t)i);
    assert(boot_flash_program(0x1F0U, data, 300U) == BOOT_FLASH_OK);
    assert(program_count == 3U && program_length[0] == 16U && program_length[1] == 256U && program_length[2] == 28U);
    assert(program_address[0] == 0x1F0U && program_address[1] == 0x200U && program_address[2] == 0x300U);
    assert(status_reads > 12U);
    assert(boot_flash_sector_erase(0x1000U) == BOOT_FLASH_OK);

    reset(); fail_tx = 6U;
    assert(boot_flash_page_program(0U, data, 256U) == BOOT_FLASH_ERROR_TIMEOUT);
    assert(!program_count); faulted();
    reset(); fail_rx = 5U;
    assert(boot_flash_page_program(0U, data, 256U) == BOOT_FLASH_ERROR_TIMEOUT);
    assert(!program_count); faulted();
    reset(); fail_tx = 2U;
    assert(boot_flash_page_program(0U, data, 256U) == BOOT_FLASH_ERROR_TIMEOUT);
    faulted();
    reset(); fail_tx = 0x20U;
    assert(boot_flash_sector_erase(0U) == BOOT_FLASH_ERROR_TIMEOUT);
    faulted();
    reset(); fail_rx = 3U;
    assert(boot_flash_read(0U, data, sizeof(data)) == BOOT_FLASH_ERROR_TIMEOUT);
    faulted();
    reset(); fail_rx = 0x9FU;
    assert(boot_flash_init(10000000U) == BOOT_FLASH_ERROR_TIMEOUT);
    {
        boot_flash_info_t info;
        boot_flash_get_info(&info);
        assert(info.faulted && !info.initialized);
    }
    reset(); stuck_busy = 1U;
    assert(boot_flash_page_program(0U, data, 256U) == BOOT_FLASH_ERROR_TIMEOUT);
    assert(status_reads > 100U && now < 600000U);
    faulted();
    reset(); /* Transport faults are recoverable only through explicit init. */
    assert(boot_flash_read(0U, data, 1U) == BOOT_FLASH_OK);
    spi_test_mhartid = 1U;
    assert(boot_flash_read(0U, data, 1U) == BOOT_FLASH_ERROR_WRONG_CORE);
    puts("BOOT_FLASH_TRANSPORT_OK");
    return 0;
}
