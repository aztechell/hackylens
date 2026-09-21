#ifndef HACKYLENS_CAPABILITY_COMMON_H
#define HACKYLENS_CAPABILITY_COMMON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t hk_result_t;

enum
{
    HK_OK = 0,
    HK_PENDING = 1,
    HK_ERR_INVALID_ARGUMENT = -1,
    HK_ERR_CAPABILITY_ABSENT = -2,
    HK_ERR_VERSION_INCOMPATIBLE = -3,
    HK_ERR_FEATURE_UNAVAILABLE = -4,
    HK_ERR_NOT_DECLARED = -5,
    HK_ERR_BUSY = -6,
    HK_ERR_WRONG_OWNER = -7,
    HK_ERR_STALE_HANDLE = -8,
    HK_ERR_WRONG_CONTEXT = -9,
    HK_ERR_INVALID_STATE = -10,
    HK_ERR_DEADLINE_EXCEEDED = -11,
    HK_ERR_CANCELLED = -12,
    HK_ERR_OVERFLOW = -13,
    HK_ERR_LIMIT = -14,
    HK_ERR_IO = -15,
    HK_ERR_INTERNAL = -16
};

typedef struct
{
    uint64_t at_us;
} hk_deadline_t;

typedef uint8_t (*hk_cancel_probe_fn)(const void *context);

typedef struct
{
    hk_cancel_probe_fn probe;
    const void *context;
} hk_cancel_t;

typedef struct
{
    void *data;
    uint32_t size_bytes;
    uint32_t stride_bytes;
    uint32_t flags;
} hk_buffer_view_t;

#define HK_BUFFER_ACCESS_READABLE (UINT32_C(1) << 0)
#define HK_BUFFER_ACCESS_WRITABLE (UINT32_C(1) << 1)

#define HK_DEADLINE_IMMEDIATE ((hk_deadline_t){0U})

#ifdef __cplusplus
}
#endif

#endif
