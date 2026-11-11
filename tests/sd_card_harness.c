#include <assert.h>
#include <stdint.h>

#include "../firmware/src/config/sd_config.h"
#include "../firmware/src/drivers/sd_spi.h"
#include "../firmware/src/storage/sd_card.h"

static uint64_t now_us;
static uint64_t command_us;
static uint32_t speed;
static uint32_t token_delay;
static uint32_t position;
static uint32_t payload_left;
static unsigned attempts;
static uint8_t selected;
static uint8_t retry_only;
static uint8_t removed;
static uint8_t writing;
static uint8_t write_timeout;
static unsigned write_payloads;
static uint8_t verify_sector[SD_BLOCK_SIZE];

uint64_t hal_time_us(void) { return now_us; }
void hal_sleep_ms(uint32_t ms) { now_us += ms * 1000U; }
uint8_t fat32_card_is_sdhc(void) { return 1U; }
void fat32_card_reset_state(void) {}
void fat32_card_set_sdhc(uint8_t value) { (void)value; }
void fat32_card_set_present(uint8_t value) { (void)value; }
uint8_t fat32_probe_silent(void) { return 1U; }
uint8_t *fat32_verify_scratch(void) { return verify_sector; }

void sd_spi_cs_high(void) { selected = 0U; }
void sd_spi_config(uint32_t hz) { assert(!selected); speed = hz; }
void sd_spi_board_init(uint32_t hz) { sd_spi_config(hz); }

void sd_spi_cs_low(void)
{
    assert(speed == (attempts ? SD_RUN_CLK_HZ / 2U : SD_RUN_CLK_HZ));
    selected = 1U;
    position = payload_left = 0U;
    attempts++;
    command_us = now_us;
}

uint8_t sd_spi_xfer(uint8_t out)
{
    now_us += 3U;
    if(!selected || removed)
        return 0xffU;
    uint32_t step = position++;
    if(step == 2U)
        assert(out == (0x40U | (writing == 2U ? 25U : writing ? 24U : 17U)));
    if(step < 8U)
        return 0xffU;
    if(step == 8U)
        return 0U;
    if(writing == 2U)
    {
        if(step == 11U || step == 17U)
            assert(out == 0xfcU);
        if(step == 14U || step == 20U)
            return 0x05U;
        if(step == 22U)
            assert(out == 0xfdU);
        return 0xffU;
    }
    if(writing)
    {
        if(step == 10U)
            assert(out == 0xfeU);
        if(step == 13U)
            return 0x05U;
        return 0xffU;
    }
    if(payload_left)
    {
        payload_left--;
        return 0xa5U;
    }
    if(now_us - command_us < token_delay || (retry_only && attempts == 1U))
        return 0xffU;
    payload_left = SD_BLOCK_SIZE + 2U;
    return 0xfeU;
}

uint8_t sd_spi_read_bytes(uint8_t *data, size_t size, uint64_t deadline_us)
{
    for(size_t i = 0U; i < size; i++)
    {
        assert(now_us < deadline_us);
        data[i] = sd_spi_xfer(0xffU);
    }
    return 1U;
}

uint8_t sd_spi_write_bytes(const uint8_t *data, size_t size, uint64_t deadline_us)
{
    assert(selected && size == SD_BLOCK_SIZE);
    assert(writing == 2U ? (position == 12U || position == 18U) : position == 11U);
    assert(deadline_us == now_us + SD_BLOCK_TRANSFER_TIMEOUT_US);
    for(size_t i = 0U; i < size; i++)
        assert(data[i] == (uint8_t)i);
    write_payloads++;
    if(write_timeout && attempts == 1U)
    {
        now_us = deadline_us;
        return 0U;
    }
    now_us += 1024U;
    return 1U;
}

static const uint8_t *write_source(void *context, uint16_t index)
{
    assert(index == write_payloads);
    return context;
}

static void reset(void)
{
    assert(!selected);
    now_us = 0U;
    attempts = 0U;
    speed = SD_RUN_CLK_HZ;
    token_delay = 9000U;
    retry_only = removed = 0U;
    writing = write_timeout = 0U;
    write_payloads = 0U;
}

int main(void)
{
    uint8_t sector[SD_BLOCK_SIZE];
    reset();
    assert(sd_card_read_block(42U, sector) && attempts == 1U);
    for(unsigned i = 0U; i < sizeof(sector); i++)
        assert(sector[i] == 0xa5U);

    reset();
    retry_only = 1U;
    assert(sd_card_read_block(42U, sector) && attempts == 2U);
    assert(now_us >= SD_READ_TIMEOUT_US && speed == SD_RUN_CLK_HZ);

    reset();
    token_delay = 2U * SD_READ_TIMEOUT_US;
    assert(!sd_card_read_block(42U, sector) && attempts == 2U);
    assert(now_us < 2U * SD_READ_TIMEOUT_US + 1000U);

    reset();
    removed = 1U;
    assert(!sd_card_read_block(42U, sector) && attempts == 2U);
    assert(now_us < 5000U);

    for(unsigned i = 0U; i < sizeof(sector); i++)
        sector[i] = (uint8_t)i;
    reset();
    writing = 1U;
    assert(sd_card_write_block_fast(42U, sector));
    assert(write_payloads == 1U && attempts == 1U && !selected);

    reset();
    writing = write_timeout = 1U;
    assert(sd_card_write_block_fast(42U, sector));
    assert(write_payloads == 2U && attempts == 2U && !selected && speed == SD_RUN_CLK_HZ);

    reset();
    writing = 2U;
    assert(sd_card_write_run(42U, 2U, write_source, sector));
    assert(write_payloads == 2U && attempts == 1U && position == 25U && !selected);
    return 0;
}
