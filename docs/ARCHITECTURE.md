# Architecture

HackyLens 0.1.0 is modular firmware built from separate C translation units under `core`, `runtime`, `controllers`, `services`, `storage`, `ui`, `drivers`, `hal`, and `apps`.

`firmware/targets/full.c` is a small composition root. It configures the runtime loop in `runtime/hk_main.c`, whose input polling and sleep timing remain platform-dependent. `core` owns app contracts, screen model, dispatch contracts, and neutral data contracts such as `core/pixel_source.h`; it does not access `hk_input` or `hal_time` directly.

## Startup

`runtime/firmware_startup.c` owns startup orchestration. It initializes the platform clocks and hardware through `runtime/platform_bootstrap.c`, loads persisted settings, applies brightness and illumination/RGB settings, initializes the boot controller, shows the boot screen, and mounts storage. The neutral autostart controller then opens the persisted registry target or falls back to the menu; it never includes feature headers.

`platform_bootstrap` is limited to board, HAL, LCD, and hardware-driver initialization. `controllers/boot_controller.c` registers shell callbacks, prepares the menu view, and writes the boot banner; feature view initialization belongs to each app's `enter` callback.

## Layer boundaries

- `drivers`, `board`, and `hal` contain hardware-specific access.
- `runtime` adapts core lifecycle and startup to platform facilities.
- `controllers` coordinate scenarios and pass state to services and UI views.
- `services` own runtime operations such as camera sessions, settings application, debug console I/O, LCD screenshot sourcing, and screenshot UART streaming.
- `storage` encodes and persists data. `storage/screenshot_bmp.c` only encodes BMP using a supplied `screenshot_pixel_source_t`; it never accesses the LCD or UART.
- `ui` renders views and is the only application-facing layer that draws through the LCD driver.

Screenshot UART output keeps the `HKSHOT BEGIN BMP24` / `HKSHOT END` protocol and CRC. `services/screenshot_source.c` supplies the LCD shadow source, `storage/screenshot_bmp.c` encodes bytes, and `services/debug_screenshot_stream.c` sends them through `services/debug_console_service.c`.

`tools/check_arch.py` guards include boundaries, forbidden compatibility headers, SDK-token placement, include cycles, and feature-module ownership.

The shared `settings_menu` controller is an instance-based UI state machine driven by a constant item descriptor table and owner callbacks. Its passive view owns only LCD rendering. It has no camera, application, storage, persistence, or screen-navigation dependencies; owner controllers retain opening/closing lifecycle and all settings side effects. CAMERA, QR, APRILTAG, and the system SETTINGS app supply separate descriptor adapters. Cycle-on-OK and edit-on-OK rows share navigation, partial redraw, hold-repeat, commit lifecycle, and optional dynamic choice providers without sharing their data models.

## Feature modules

All ten menu applications are self-contained modules: `apps/terminal/`, `apps/camera/`, `apps/qr_camera/`, `apps/face_detect/`, `apps/apriltag/`, `apps/files/`, `apps/buttons/`, `apps/pong/`, `apps/settings/`, and `apps/sleep/`. Each owns its app entry point, controller, view, icon, feature configuration, and feature-specific state/services. The only public header of a module is its `*_app.h`, and only `apps/app_registry.c` may include it.

The build manifest maps each app ID to its whole directory. `--disable-app` therefore removes every source and private header of that feature. Shared camera sources remain while CAMERA, QR-CAMERA, FACE DETECT, or APRILTAG is enabled; `quirc` is staged only for QR-CAMERA. With no camera consumer, sensor/DVP/camera runtime sources are omitted while the general KPU HAL remains available.

The registry dispatches primary and secondary screen ownership, lifecycle callbacks, background ticks, SD events, menu icons, and debug commands. Shared screen, SD, debug, boot, and system-tick controllers do not include feature headers or select features with conditionals.

CAMERA owns photo capture orchestration, encoders/writers, photo paths, settings adapter, and its view. QR-CAMERA owns quirc integration, luma conversion, result state/view, text persistence, settings, and its view. Both reuse the shared camera session, sensor/frame pipeline, camera preview renderer, and settings persistence.

FILES owns browser navigation/state, previews and deletion, all BMP/PNG/PPM/RAW/GIF decoders, and its view. Shared FAT32 provides neutral mount, directory scan, file, allocation, and stream contracts and has no dependency on FILES browser state. BUTTONS, system SETTINGS, and SLEEP likewise own their controllers and views; SLEEP receives the input snapshot through the registry background lifecycle for auto-sleep.

FACE DETECT owns its detector adapter, model-storage adapter, view, icon, debug command, and deferred KPU-unload lifecycle. It reuses the shared camera runtime, FAT32 implementation, and KPU/DVP HAL. Its model is loaded from `/hackylens.kmodels/detect.kmodel` on the SD card; it is not embedded in firmware flash.

APRILTAG owns its TAG36H11 detector adapter, grayscale downsampler, settings/selection model and descriptor adapter, stabilized in-frame result overlay, icon, and `HKTAG`/`HKTAGINFO` commands. Its descriptor adapter uses the shared camera-independent `settings_menu`, while APRILTAG retains persistence and camera pause/resume policy. It reuses the shared 320x240 camera runtime and publishes native tag IDs through `vision_result_service` as BLOCK results. Core 0 downsamples a leased camera frame only when the single uncached 160x120 luma handoff is free and immediately releases the frame; frames seen while core 1 is busy are intentionally discarded instead of queued, preventing stale-result latency. A persistent worker on core 1 owns the speed-optimized CPU detector and atomically publishes completed result banks. `refine_edges` is a persisted runtime request applied by core 1 before the next detection rather than a detector restart. The app uses camera session overrides for its independent FPS and LED/RGB profile, leaving CAMERA and QR settings unchanged. Its `ALL/SELECTED` filter is applied before the shared result snapshot, so the UART/I2C wire format remains unchanged. Preview overlays are composed into the LCD shadow before the full-frame transfer, so rectangles are not temporarily erased by the following camera frame. The detector does not use a KPU model. The BSD-licensed OpenMV AprilTag core is staged from `firmware/third_party/apriltag` only when this feature is enabled.

Terminal owns its bounded line ring, viewport, scrolling, font geometry, and log-sink lifecycle. The shared logging service exposes only a generic optional sink and remains independent of Terminal. Font selection remains in reserved feature bits inherited from the legacy settings payload, so old records and erased flash continue to decode as `TERMINAL_FONT_NORMAL`.

Settings record v3 retains the v2 settings prefix and fixed 80-byte opaque app-data block, then appends one stable autostart ID byte. The storage layer validates and migrates v1, v2, and v3 records; v1/v2 migration defaults autostart to OFF without changing prior CAMERA, external-link, or APRILTAG data. APRILTAG alone interprets the opaque app-data schema and 587-bit selected-ID map.

## Architecture guard

`tools/check_arch.py` uses one declarative table for all ten feature directories. It rejects legacy paths, flat app implementations, external inclusion of private feature headers, private settings-menu view access, layer inversions, include cycles, and a mismatch between feature directories and the build manifest.
