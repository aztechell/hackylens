#ifndef HK_KIBBLE_SESSION_H
#define HK_KIBBLE_SESSION_H

#include <hackylens/app/kibble.h>

/* Core 0 only. The client keeps a unique owner token until teardown. */
hk_result_t kibble_session_open_step(const void *owner, const char **status);
hk_result_t kibble_session_start(const void *owner, const char *prompt, uint32_t seed);
hk_result_t kibble_session_poll(const void *owner, hk_kibble_token_t *token);
void kibble_session_close(const void *owner, hk_deadline_t deadline);

#endif
