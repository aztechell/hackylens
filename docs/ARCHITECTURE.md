# Architecture

HackyLens 0.1.0 is modular firmware built from separate C translation units under `core`, `runtime`, `controllers`, `services`, `storage`, `ui`, `drivers`, `hal`, and `apps`.

`firmware/targets/full.c` is a small composition root. It configures the runtime loop in `runtime/hk_main.c`, whose input polling and sleep timing remain platform-dependent. `core` owns app contracts, screen model, dispatch contracts, and neutral data contracts such as `core/pixel_source.h`; it does not access `hk_input` or `hal_time` directly.

## Startup

`runtime/firmware_startup.c` owns startup orchestration. It initializes the platform clocks and hardware through `runtime/platform_bootstrap.c`, loads persisted settings, applies brightness and illumination/RGB settings, initializes the boot controller, shows the boot screen, mounts storage, and enters the menu.

`platform_bootstrap` is limited to board, HAL, LCD, and hardware-driver initialization. `controllers/boot_controller.c` registers shell callbacks, prepares the menu view, writes the boot banner, and initializes optional UI state; it does not depend on runtime or low-level board/HAL headers.

## Layer boundaries

- `drivers`, `board`, and `hal` contain hardware-specific access.
- `runtime` adapts core lifecycle and startup to platform facilities.
- `controllers` coordinate scenarios and pass state to services and UI views.
- `services` own runtime operations such as camera sessions, settings application, debug console I/O, LCD screenshot sourcing, and screenshot UART streaming.
- `storage` encodes and persists data. `storage/screenshot_bmp.c` only encodes BMP using a supplied `screenshot_pixel_source_t`; it never accesses the LCD or UART.
- `ui` renders views and is the only application-facing layer that draws through the LCD driver.

Screenshot UART output keeps the `HKSHOT BEGIN BMP24` / `HKSHOT END` protocol and CRC. `services/screenshot_source.c` supplies the LCD shadow source, `storage/screenshot_bmp.c` encodes bytes, and `services/debug_screenshot_stream.c` sends them through `services/debug_console_service.c`.

`tools/check_arch.py` guards include boundaries, forbidden compatibility headers, SDK-token placement, include cycles, and feature-module ownership.

The shared `settings_menu` controller is an instance-based UI state machine driven by a constant item descriptor table and owner callbacks. Its passive view owns only LCD rendering. It has no camera, application, storage, persistence, or screen-navigation dependencies; owner controllers retain opening/closing lifecycle and all settings side effects. CAMERA, QR, APRILTAG, and the system SETTINGS app supply separate descriptor adapters. Cycle-on-OK and edit-on-OK rows share navigation, partial redraw, hold-repeat, and commit lifecycle without sharing their data models.

## Feature modules

`apps/pong/`, `apps/terminal/`, `apps/face_detect/`, and `apps/apriltag/` are self-contained app modules. Their app entry points, controllers, views, configuration, state/types, and menu icons live inside each feature directory. The app registry is the sole shared-layer connection to each module and includes only its public app header; the corresponding feature flag and build manifest select each module as a unit. Feature code may use core lifecycle, input, LCD/UI, logging, settings, and menu/back-navigation contracts, while drivers, HAL, runtime, menu, and shared UI remain in the layered architecture.

FACE DETECT owns its detector adapter, model-storage adapter, view, icon, debug command, and deferred KPU-unload lifecycle. It reuses the shared camera runtime, FAT32 implementation, and KPU/DVP HAL. Its model is loaded from `/hackylens.kmodels/detect.kmodel` on the SD card; it is not embedded in firmware flash.

APRILTAG owns its TAG36H11 detector adapter, grayscale downsampler, settings/selection model and descriptor adapter, stabilized in-frame result overlay, icon, and `HKTAG`/`HKTAGINFO` commands. Its descriptor adapter uses the shared camera-independent `settings_menu`, while APRILTAG retains persistence and camera pause/resume policy. It reuses the shared 320x240 camera runtime and publishes native tag IDs through `vision_result_service` as BLOCK results. Core 0 downsamples a leased camera frame only when the single uncached 160x120 luma handoff is free and immediately releases the frame; frames seen while core 1 is busy are intentionally discarded instead of queued, preventing stale-result latency. A persistent worker on core 1 owns the speed-optimized CPU detector and atomically publishes completed result banks. `refine_edges` is a persisted runtime request applied by core 1 before the next detection rather than a detector restart. The app uses camera session overrides for its independent FPS and LED/RGB profile, leaving CAMERA and QR settings unchanged. Its `ALL/SELECTED` filter is applied before the shared result snapshot, so the UART/I2C wire format remains unchanged. Preview overlays are composed into the LCD shadow before the full-frame transfer, so rectangles are not temporarily erased by the following camera frame. The detector does not use a KPU model. The BSD-licensed OpenMV AprilTag core is staged from `firmware/third_party/apriltag` only when this feature is enabled.

Terminal owns its bounded line ring, viewport, scrolling, font geometry, and log-sink lifecycle. The shared logging service exposes only a generic optional sink and remains independent of Terminal. Font selection remains in reserved feature bits inherited from the legacy settings payload, so old records and erased flash continue to decode as `TERMINAL_FONT_NORMAL`.

Settings record v2 retains the legacy 16-byte settings prefix and adds a fixed 80-byte opaque app-data block. The storage layer validates and migrates both v1 and v2 records; APRILTAG alone interprets its app-data schema and 587-bit selected-ID map. Keeping the block opaque and layout-stable preserves it across builds where APRILTAG is disabled.

## Architecture freeze

The current Layered Architecture is complete. `controllers` may continue to use `hal_time` as an embedded shortcut; `runtime` is the composition root and may connect lower layers. Future file moves are allowed only to fix a concrete bug, add a platform, or introduce a subsystem. Refactoring solely to raise a perceived architecture-cleanliness percentage is prohibited.
