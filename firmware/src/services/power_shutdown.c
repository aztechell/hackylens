#include "power_shutdown.h"
#include "settings_persistence.h"
#include "hk_config.h"
#include "hal_time.h"
#if HK_ENABLE_APP_MICROPYTHON
#include "micropython_runtime.h"
#include "../storage/userfs.h"
static uint8_t s_remount;
#endif
void power_shutdown_restore(void)
{
#if HK_ENABLE_APP_MICROPYTHON
    if(s_remount) (void)userfs_mount();
    s_remount = 0U;
#endif
}
hk_result_t power_shutdown_prepare(hk_deadline_t deadline)
{
#if HK_ENABLE_APP_MICROPYTHON
    micropython_runtime_status_t runtime;
    userfs_status_t filesystem;
#endif
    if(deadline.at_us && hal_time_us() >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
#if HK_ENABLE_APP_MICROPYTHON
    micropython_runtime_get_status(&runtime);
    if(runtime.state == MICROPYTHON_RUNTIME_STARTING ||
       runtime.state == MICROPYTHON_RUNTIME_RUNNING ||
       runtime.state == MICROPYTHON_RUNTIME_STOPPING)
    {
        (void)micropython_runtime_request_stop();
        return HK_PENDING;
    }
#endif
    if(!settings_storage_flush()) return HK_ERR_IO;
#if HK_ENABLE_APP_MICROPYTHON
    userfs_get_status(&filesystem);
    if(filesystem.upload_id && userfs_upload_abort(filesystem.upload_id) != USERFS_OK)
        return HK_ERR_IO;
    if(filesystem.state == USERFS_STATE_MOUNTED)
    {
        if(userfs_unmount() != USERFS_OK) return HK_ERR_IO;
        s_remount = 1U;
    }
#endif
    if(deadline.at_us && hal_time_us() >= deadline.at_us)
    {
        power_shutdown_restore();
        return HK_ERR_DEADLINE_EXCEEDED;
    }
    return HK_OK;
}
