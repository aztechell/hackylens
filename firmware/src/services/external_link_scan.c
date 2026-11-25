#include "external_link_scan.h"
#include "external_link_service.h"
#include "../capabilities/external_link_provider.h"

enum
{
    FIRST_ADDRESS = 0x08U,
    LAST_ADDRESS = 0x77U,
    ADDRESSES_PER_STEP = 8U,
};

static void finish(external_link_scan_t *scan, hk_result_t result)
{
    if(scan->link.service)
    {
        hk_result_t closed = hk_external_link_retire(&scan->link, HK_DEADLINE_IMMEDIATE);
        external_link_service_restore(&scan->link);
        if(result == HK_OK)
            result = closed;
    }
    scan->result = result;
}

void external_link_scan_start(external_link_scan_t *scan)
{
    if(scan->link.service)
        return;
    *scan = (external_link_scan_t){.next_address = FIRST_ADDRESS};
    const hk_external_link_i2c_controller_config_t config = {
        sizeof(config), HK_EXTERNAL_LINK_I2C_CONTROLLER_CONFIG_VERSION, 100000U, 0U,
    };
    hk_result_t result = external_link_service_claim(
        HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER, &scan->link);
    if(result == HK_OK)
        result = hk_external_link_configure_i2c_controller(&scan->link, &config);
    if(result == HK_OK)
        scan->result = HK_PENDING;
    else
        finish(scan, result);
}

void external_link_scan_stop(external_link_scan_t *scan)
{
    if(scan->result == HK_PENDING)
        finish(scan, HK_ERR_CANCELLED);
}

uint8_t external_link_scan_step(external_link_scan_t *scan)
{
    if(scan->result != HK_PENDING)
        return 0U;
    for(unsigned batch = 0U; batch < ADDRESSES_PER_STEP; ++batch)
    {
        const uint8_t address = scan->next_address++;
        uint8_t ack;
        hk_result_t result = hk_external_link_i2c_probe(&scan->link, address, &ack);
        if(result == HK_OK && ack)
        {
            scan->found[address / 8U] |= 1U << (address % 8U);
            ++scan->count;
        }
        if(result != HK_OK || address == LAST_ADDRESS)
        {
            finish(scan, result);
            return 1U;
        }
    }
    return 0U;
}

uint8_t external_link_scan_address(const external_link_scan_t *scan, uint8_t index)
{
    for(uint8_t address = FIRST_ADDRESS; address <= LAST_ADDRESS; ++address)
        if(scan->found[address / 8U] & (1U << (address % 8U)))
        {
            if(index == 0U)
                return address;
            --index;
        }
    return 0U;
}
