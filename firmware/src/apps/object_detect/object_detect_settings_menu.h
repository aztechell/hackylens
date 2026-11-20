#ifndef HK_OBJECT_DETECT_SETTINGS_MENU_H
#define HK_OBJECT_DETECT_SETTINGS_MENU_H

#include "../../controllers/settings_menu_controller.h"

const settings_menu_definition_t *object_detect_settings_menu_definition(void);
settings_menu_event_t object_detect_settings_menu_handle_input(
    settings_menu_session_t *session, const hk_input_snapshot_t *input);

#endif
