#ifndef HK_QR_CAMERA_FIRMWARE_H
#define HK_QR_CAMERA_FIRMWARE_H

#ifndef HACKYLENS_VERSION
#include "hk_config.h"
#endif
#include <hackylens/capability/time.h>

#include "quirc.h"
#include "../../config/display_config.h"
#include "../../config/fat32_config.h"
#include "../../config/input_config.h"
#include "../../config/menu_layout.h"
#include "../../config/sd_config.h"
#include "../../config/settings_config.h"
#include "../../controllers/camera_runtime_controller.h"
#include "../../controllers/settings_menu_controller.h"
#include "../../core/camera_types.h"
#include "../../core/hk_app.h"
#include "../../core/hk_app_registry.h"
#include "../../core/hk_back_exit.h"
#include "../../core/hk_binary.h"
#include "../../core/hk_capability_client.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_menu_runtime.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "../../services/camera_frame.h"
#include "../../services/camera_input.h"
#include "../../services/camera_light.h"
#include "../../services/camera_persist_settings.h"
#include "../../services/camera_session.h"
#include "../../services/debug_console_service.h"
#include "../../services/settings_lights.h"
#include "../../services/settings_persistence.h"
#include "../../services/settings_service.h"
#include "../../storage/fat32_allocation.h"
#include "../../storage/fat32_directory.h"
#include "../../storage/fat32_types.h"
#include "../../storage/fat32_volume.h"
#include "../../storage/file_dir_scan.h"
#include "../../storage/file_mount.h"
#include "../../storage/file_write_error.h"
#include "../../storage/internal/fat32_state_private.h"
#include "../../storage/sd_card.h"
#include "../../ui/camera_view.h"
#include "../../ui/display_binding.h"

#endif
