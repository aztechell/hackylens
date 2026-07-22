#ifndef HK_APP_H
#define HK_APP_H

#include <stdint.h>

typedef enum
{
    SCREEN_MENU = 0,
    SCREEN_CAMERA,
    SCREEN_QR_CAMERA,
    SCREEN_FACE_DETECT,
    SCREEN_APRILTAG,
    SCREEN_CAMERA_SETTINGS,
    SCREEN_FILES,
    SCREEN_BUTTONS,
    SCREEN_APP_SLOT_0,
    SCREEN_APP_SLOT_1,
    SCREEN_APP_SLOT_2,
    SCREEN_APP_SLOT_3,
    SCREEN_SETTINGS,
    SCREEN_SLEEP,
} screen_t;

typedef struct
{
    uint32_t state;
    uint32_t pressed;
    uint32_t changed;
} hk_input_snapshot_t;

typedef struct hk_app
{
    const char *id;
    const char *title;
    screen_t screen;
    void (*enter)(const hk_input_snapshot_t *input);
    void (*exit)(void);
    void (*tick)(const hk_input_snapshot_t *input);
    void (*handle_input)(const hk_input_snapshot_t *input);
    void (*draw_icon)(uint16_t x, uint16_t y, uint16_t color, uint16_t bg);
    void (*background_tick)(void);
    uint8_t (*handle_debug_command)(const char *cmd);
    const char *debug_help;
} hk_app_t;

#endif
