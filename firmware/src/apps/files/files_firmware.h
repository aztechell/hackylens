#ifndef HK_FILES_FIRMWARE_H
#define HK_FILES_FIRMWARE_H

#ifndef HACKYLENS_VERSION
#include "hk_config.h"
#endif
#include <hackylens/capability/time.h>

#include "../../config/display_config.h"
#include "../../config/fat32_config.h"
#include "../../config/input_config.h"
#include "../../config/sd_config.h"
#include "../../core/camera_types.h"
#include "../../core/file_name.h"
#include "../../core/hk_app.h"
#include "../../core/hk_back_exit.h"
#include "../../core/hk_binary.h"
#include "../../core/hk_camera_sizes.h"
#include "../../core/hk_capability_client.h"
#include "../../core/hk_events.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_string.h"
#include "../../services/frame_workspace.h"
#include "../../storage/fat32_allocation.h"
#include "../../storage/fat32_directory.h"
#include "../../storage/fat32_file.h"
#include "../../storage/fat32_stream.h"
#include "../../storage/fat32_types.h"
#include "../../storage/fat32_volume.h"
#include "../../storage/file_mount.h"
#include "../../storage/internal/fat32_state_private.h"
#include "../../storage/sd_card.h"
#include "../../ui/display_binding.h"
#include "../../ui/hk_ui.h"

#endif
