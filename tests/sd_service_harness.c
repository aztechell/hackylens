#include <assert.h>
#include <stdint.h>

#include "sd_service.h"

static unsigned probes;
static unsigned removed;
static uint8_t healthy = 1U;
static uint8_t mounted = 1U;

uint8_t hk_sd_present(void) { return mounted; }
uint8_t hk_fat_mounted(void) { return mounted; }
uint8_t file_mount_if_needed(void) { return mounted; }
void fat32_set_probe_silent(uint8_t silent) { (void)silent; }

uint8_t fat32_health_check(void)
{
    probes++;
    return healthy;
}

void fat32_mark_removed(void)
{
    removed++;
    mounted = 0U;
}

static hk_sd_event_t next_probe(void)
{
    unsigned before = probes;
    for(unsigned tick = 0U; tick < 60U; tick++)
    {
        hk_sd_event_t event = sd_service_tick();
        if(probes != before)
            return event;
        assert(event == HK_SD_EVENT_NONE);
    }
    assert(0);
    return HK_SD_EVENT_ERROR;
}

int main(void)
{
    assert(next_probe() == HK_SD_EVENT_NONE);
    healthy = 0U;
    assert(next_probe() == HK_SD_EVENT_NONE);
    assert(next_probe() == HK_SD_EVENT_NONE);
    assert(mounted && !removed);
    healthy = 1U;
    assert(next_probe() == HK_SD_EVENT_NONE);
    healthy = 0U;
    assert(next_probe() == HK_SD_EVENT_NONE);
    assert(next_probe() == HK_SD_EVENT_NONE);
    assert(next_probe() == HK_SD_EVENT_REMOVED);
    assert(removed == 1U && !mounted);
    return 0;
}
