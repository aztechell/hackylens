#include "../core/hk_screen.h"

#include "../core/hk_app.h"
#include "../hal/hal_time.h"

static screen_t s_screen = SCREEN_MENU;
static uint64_t s_last_activity_us;

screen_t hk_screen_get(void)
{
    return s_screen;
}

void hk_screen_set(screen_t screen)
{
    s_screen = screen;
}

const char *screen_label(screen_t screen)
{
    if(screen == SCREEN_CAMERA)
        return "CAMERA";
    if(screen == SCREEN_QR_CAMERA)
        return "QR-CAMERA";
    if(screen == SCREEN_FACE_DETECT)
        return "FACE-DETECT";
    if(screen == SCREEN_CAMERA_SETTINGS)
        return "CAM-SETTINGS";
    if(screen == SCREEN_FILES)
        return "FILES";
    if(screen == SCREEN_BUTTONS)
        return "BUTTONS";
    if(screen == SCREEN_SETTINGS)
        return "SETTINGS";
    if(screen == SCREEN_SLEEP)
        return "SLEEP";
    return "MENU";
}

uint64_t hk_last_activity_us(void)
{
    return s_last_activity_us;
}

void activity_note(void)
{
    s_last_activity_us = hal_time_us();
}
