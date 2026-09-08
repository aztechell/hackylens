#ifndef HK_TEST_LIGHTS_NORMATIVE_BACKEND_H
#define HK_TEST_LIGHTS_NORMATIVE_BACKEND_H

#include <stdint.h>

const char *lights_normative_backend_name(void);
void lights_normative_backend_reset(uint64_t now_us);
void lights_normative_backend_set_now(uint64_t now_us);
uint32_t lights_normative_backend_effect_count(void);
uint32_t lights_normative_backend_active_mask(void);
uint32_t lights_normative_backend_safe_off_mask(void);

#endif
