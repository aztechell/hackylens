#ifndef HK_SETTINGS_APP_MENU_H
#define HK_SETTINGS_APP_MENU_H

#include "../../controllers/settings_menu_controller.h"
#include "../../storage/userfs.h"

typedef enum
{
    SETTINGS_PAGE_ROOT = 0,
    SETTINGS_PAGE_SCREEN,
    SETTINGS_PAGE_LIGHTS,
    SETTINGS_PAGE_CONNECTION,
    SETTINGS_PAGE_STARTUP,
    SETTINGS_PAGE_DEVICE,
    SETTINGS_PAGE_SOUND,
    SETTINGS_PAGE_RGB,
} settings_page_t;

enum
{
    SETTINGS_SD_PROGRESS_STEP = 10,
    SETTINGS_RGB_RED = 32,
    SETTINGS_RGB_GREEN,
    SETTINGS_RGB_BLUE,
};

const settings_menu_definition_t *settings_app_menu_definition(void);
const settings_menu_definition_t *settings_app_page_definition(settings_page_t page);

typedef struct
{
    settings_menu_definition_t definition;
    char names[USERFS_PIN_MAX][USERFS_NAME_MAX + 1U];
    uint8_t count;
} settings_startup_choices_t;

const settings_menu_definition_t *settings_app_startup_choices(settings_startup_choices_t *choices);

#endif
