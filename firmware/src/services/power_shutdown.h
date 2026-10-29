#ifndef HK_POWER_SHUTDOWN_H
#define HK_POWER_SHUTDOWN_H
#include <hackylens/capability/common.h>
hk_result_t power_shutdown_prepare(hk_deadline_t deadline);
void power_shutdown_restore(void);
#endif
