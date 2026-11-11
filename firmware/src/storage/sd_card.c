#include "internal/fat32_state_private.h"

#include <stddef.h>

#include <stdio.h>
#include <string.h>

#include "../config/sd_config.h"
#include "fat32_volume.h"
#include "sd_card.h"

#include "../drivers/sd_spi.h"
#include "hal_time.h"

static void sd_deselect(void)
{
    sd_spi_cs_high();
    sd_spi_xfer(0xFF);
}

static uint8_t sd_wait_ready(void)
{
    uint64_t deadline = hal_time_us() + SD_WRITE_TIMEOUT_US;
    do
    {
        if(sd_spi_xfer(0xFF) == 0xFF)
            return 1;
    } while(hal_time_us() < deadline);
    return 0;
}

static uint8_t sd_command(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *extra, uint8_t extra_len, uint8_t release)
{
    uint8_t r1 = 0xFF;

    sd_spi_cs_low();
    sd_spi_xfer(0xFF);
    if(cmd != 0 && !sd_wait_ready())
    {
        if(release)
            sd_deselect();
        return 0xFF;
    }
    sd_spi_xfer((uint8_t)(0x40U | cmd));
    sd_spi_xfer((uint8_t)(arg >> 24));
    sd_spi_xfer((uint8_t)(arg >> 16));
    sd_spi_xfer((uint8_t)(arg >> 8));
    sd_spi_xfer((uint8_t)arg);
    sd_spi_xfer(crc);

    for(uint16_t i = 0; i < SD_CMD_TIMEOUT; i++)
    {
        r1 = sd_spi_xfer(0xFF);
        if((r1 & 0x80U) == 0)
            break;
    }

    if(extra && extra_len)
    {
        for(uint8_t i = 0; i < extra_len; i++)
            extra[i] = sd_spi_xfer(0xFF);
    }

    if(release)
        sd_deselect();
    return r1;
}

static uint8_t sd_read_block_once(uint32_t lba, uint8_t *dst)
{
    uint32_t arg = fat32_card_is_sdhc() ? lba : lba * SD_BLOCK_SIZE;
    uint8_t r1 = sd_command(17, arg, 0x01, NULL, 0, 0);

    if(r1 != 0x00)
    {
        sd_deselect();
        return 0;
    }

    uint64_t deadline = hal_time_us() + SD_READ_TIMEOUT_US;
    do
    {
        uint8_t token = sd_spi_xfer(0xFF);
        if(token == 0xFE)
        {
            uint8_t result = sd_spi_read_bytes(dst, SD_BLOCK_SIZE,
                                               hal_time_us() + SD_BLOCK_TRANSFER_TIMEOUT_US);
            if(result)
            {
                sd_spi_xfer(0xFF);
                sd_spi_xfer(0xFF);
            }
            sd_deselect();
            return result;
        }
        if(token && token < 0x10U)
            break;
    } while(hal_time_us() < deadline);

    sd_deselect();
    return 0;
}

static void sd_retry_bus(void)
{
    sd_spi_cs_high();
    sd_spi_config(SD_RUN_CLK_HZ / 2U);
    for(uint8_t i = 0U; i < 8U; i++)
        sd_spi_xfer(0xFF);
}

uint8_t sd_card_read_block(uint32_t lba, uint8_t *dst)
{
    if(sd_read_block_once(lba, dst))
        return 1U;
    sd_retry_bus();
    uint8_t result = sd_read_block_once(lba, dst);
    sd_spi_config(SD_RUN_CLK_HZ);
    return result;
}

static uint8_t sd_write_payload(uint8_t token, const uint8_t *src)
{
    sd_spi_xfer(0xFF);
    sd_spi_xfer(token);
    if(!sd_spi_write_bytes(src, SD_BLOCK_SIZE, hal_time_us() + SD_BLOCK_TRANSFER_TIMEOUT_US))
        return 0xffU;
    sd_spi_xfer(0xFF);
    sd_spi_xfer(0xFF);

    for(uint8_t i = 0; i < 64; i++)
    {
        uint8_t response = sd_spi_xfer(0xFF);
        if(response != 0xffU)
            return response;
    }
    return 0xffU;
}

static uint8_t sd_write_block_checked(uint32_t lba, const uint8_t *src, uint8_t verify)
{
    uint32_t arg = fat32_card_is_sdhc() ? lba : lba * SD_BLOCK_SIZE;
    uint8_t r1 = sd_command(24, arg, 0x01, NULL, 0, 0);
    uint8_t response;

    if(r1 != 0x00)
    {
        sd_deselect();
        printf("[SD] CMD24 command failed lba=%u r1=0x%02X\r\n", lba, r1);
        return 0;
    }

    response = sd_write_payload(0xfeU, src);
    if((response & 0x1FU) != 0x05)
    {
        sd_deselect();
        printf("[SD] CMD24 data reject lba=%u resp=0x%02X\r\n", lba, response);
        return 0;
    }

    if(sd_wait_ready())
    {
        sd_deselect();
        if(!verify)
            return 1;
        uint8_t *verify_sector = fat32_verify_scratch();
        if(!sd_read_block_once(lba, verify_sector))
        {
            printf("[SD] CMD24 verify read failed lba=%u\r\n", lba);
            return 0;
        }
        if(memcmp(src, verify_sector, SD_BLOCK_SIZE) != 0)
        {
            printf("[SD] CMD24 verify mismatch lba=%u\r\n", lba);
            return 0;
        }
        return 1;
    }

    sd_deselect();
    printf("[SD] CMD24 busy timeout lba=%u\r\n", lba);
    return 0;
}

static uint8_t sd_write_block(uint32_t lba, const uint8_t *src, uint8_t verify)
{
    if(sd_write_block_checked(lba, src, verify))
        return 1U;
    sd_retry_bus();
    uint8_t result = sd_write_block_checked(lba, src, verify);
    sd_spi_config(SD_RUN_CLK_HZ);
    return result;
}

uint8_t sd_card_write_block(uint32_t lba, const uint8_t *src)
{
    return sd_write_block(lba, src, 1U);
}

uint8_t sd_card_write_block_fast(uint32_t lba, const uint8_t *src)
{
    return sd_write_block(lba, src, 0U);
}

uint8_t sd_card_write_run(uint32_t lba, uint16_t count,
                          sd_block_source_t source, void *context)
{
    if(!source || !count || lba > UINT32_MAX - (count - 1U))
        return 0U;

    if(count > 1U)
    {
        uint32_t arg = fat32_card_is_sdhc() ? lba : lba * SD_BLOCK_SIZE;
        uint8_t response = sd_command(25U, arg, 0x01U, NULL, 0U, 0U);
        if(response == 0U)
        {
            uint16_t written = 0U;
            while(written < count && sd_wait_ready())
            {
                const uint8_t *data = source(context, written);
                if(!data || (sd_write_payload(0xfcU, data) & 0x1fU) != 0x05U)
                    break;
                written++;
            }
            uint8_t ready = sd_wait_ready();
            if(ready)
            {
                sd_spi_xfer(0xfdU); /* SPI Stop Tran token for CMD25. */
                sd_spi_xfer(0xffU);
                ready = sd_wait_ready();
            }
            sd_deselect();
            if(written == count && ready)
                return 1U;
            printf("[SD] CMD25 incomplete lba=%u blocks=%u/%u\r\n", lba, written, count);
            return 0U;
        }
        sd_deselect();
        if(response != 0x04U) /* Fall back only on an explicit illegal-command response. */
            return 0U;
    }

    for(uint16_t index = 0U; index < count; index++)
    {
        const uint8_t *data = source(context, index);
        if(!data || !sd_card_write_block_fast(lba + index, data))
            return 0U;
    }
    return 1U;
}

uint8_t sd_card_init(void)
{
    uint8_t r1;
    uint8_t r7[4] = {0};
    uint8_t ocr[4] = {0};
    uint8_t v2_card = 0;

    fat32_card_reset_state();
    sd_spi_board_init(SD_INIT_CLK_HZ);

    for(uint8_t i = 0; i < 12; i++)
        sd_spi_xfer(0xFF);

    r1 = sd_command(0, 0, 0x95, NULL, 0, 1);
    if(r1 != 0x01)
    {
        if(!fat32_probe_silent())
            printf("[SD] CMD0 failed r1=0x%02X\r\n", r1);
        return 0;
    }

    r1 = sd_command(8, 0x000001AAUL, 0x87, r7, sizeof(r7), 1);
    if(r1 == 0x01 && r7[2] == 0x01 && r7[3] == 0xAA)
        v2_card = 1;

    for(uint16_t i = 0; i < 1000; i++)
    {
        (void)sd_command(55, 0, 0x01, NULL, 0, 1);
        r1 = sd_command(41, v2_card ? 0x40000000UL : 0, 0x01, NULL, 0, 1);
        if(r1 == 0x00)
            break;
        hal_sleep_ms(1);
    }
    if(r1 != 0x00)
    {
        if(!fat32_probe_silent())
            printf("[SD] ACMD41 failed r1=0x%02X\r\n", r1);
        return 0;
    }

    r1 = sd_command(58, 0, 0x01, ocr, sizeof(ocr), 1);
    if(r1 == 0x00 && (ocr[0] & 0x40U))
        fat32_card_set_sdhc(1);
    if(!fat32_card_is_sdhc())
        (void)sd_command(16, SD_BLOCK_SIZE, 0x01, NULL, 0, 1);

    sd_spi_config(SD_RUN_CLK_HZ);
    fat32_card_set_present(1);
    printf("[SD] card init ok type=%s\r\n", fat32_card_is_sdhc() ? "SDHC" : "SDSC");
    return 1;
}
