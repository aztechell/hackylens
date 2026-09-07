#ifndef HK_TEST_INPUT_NORMATIVE_BACKEND_H
#define HK_TEST_INPUT_NORMATIVE_BACKEND_H

#include <hackylens/capability/input.h>

const char *input_normative_backend_name(void);
void input_normative_backend_reset(void);
hk_result_t input_normative_backend_sample(uint64_t timestamp_us,
                                           uint32_t raw_state);

#endif
