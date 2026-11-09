#ifndef HACKYLENS_APP_ACCELEROMETER_H
#define HACKYLENS_APP_ACCELEROMETER_H

#include <hackylens/app/context.h>
#include <hackylens/capability/accelerometer.h>

#ifdef __cplusplus
extern "C" {
#endif

hk_result_t hk_app_context_accelerometer(const hk_app_context_t *ctx,
                                         const hk_accelerometer_t **accelerometer);

#ifdef __cplusplus
}
#endif

#endif
