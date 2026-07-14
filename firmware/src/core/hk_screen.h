#ifndef HK_SCREEN_H
#define HK_SCREEN_H

#include "hk_app.h"


screen_t hk_screen_get(void);
void hk_screen_set(screen_t screen);
const char *screen_label(screen_t screen);
uint64_t hk_last_activity_us(void);
void activity_note(void);

#endif
