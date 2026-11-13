#ifndef HK_SETTINGS_FIRMWARE_H
#define HK_SETTINGS_FIRMWARE_H

#ifndef HACKYLENS_VERSION
#include "hk_config.h"
#endif
#include "../../core/hk_app_registry.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_ui_colors.h"
#include "../../services/external_link_service.h"
#include "../../services/external_link_types.h"
#include "../../services/settings_lights.h"
#include "../../services/settings_persistence.h"
#include "../../services/settings_service.h"
#include "../../services/audio_service.h"
#include "../../storage/fat32_volume.h"
#if HK_ENABLE_APP_MICROPYTHON
#include "../../storage/userfs.h"
#endif

#endif
