# Modules

Default enabled apps: TERMINAL, CAMERA, QR-CAMERA, FACE DETECT, FILES, BUTTONS, PONG, SETTINGS, SLEEP.

Compile-time app flags are generated into `hk_config.h` by `tools/build_firmware.py`. The app registry lives in `apps/app_registry.c`. `--disable-app pong` removes PONG and its complete `apps/pong/` implementation. Likewise, `--disable-app terminal` omits all of `apps/terminal/` (app, controller, view, buffer, configuration, and feature types). Shared logging, debug-console, UART, and settings services remain available to other features.

Key public interfaces:

- `core/hk_app.h`, `core/hk_app_registry.h`, and `core/hk_screen.h` for app metadata, lookup, and screen model.
- `core/pixel_source.h` for a neutral pixel-reader contract.
- `runtime/hk_main.h` and `runtime/firmware_startup.h` for the platform loop and startup composition.
- `services/settings_persistence.h` and `services/settings_lights.h` for settings load and application.
- `services/debug_console_service.h` for narrow debug UART read/write access.
- `services/debug_screenshot_stream.h` and `services/screenshot_source.h` for screenshot UART transport and LCD shadow sourcing.
- `drivers/camera_stream.h` for the IRQ-driven two-slot camera stream and its explicit frame-lease contract; SDK interrupt details remain private to `hal/hal_dvp.c`.
- `services/face_detector.h` for KPU face detection and `storage/face_model_storage.h` for the model stored in flash.
- `drivers/hk_lcd.h` for the synchronous full-frame RGB565-BE surface lease; UI composes into the existing LCD shadow before a single driver-owned SPI present.
- `storage/screenshot_bmp.h` for BMP encoding, `storage/screenshot_writer.h` for persistence, and focused FAT32/file headers for storage operations.
- `ui/hk_ui.h` and per-screen view headers for UI rendering.

Private headers are allowed only within their subsystem boundary; they are not compatibility facades or new global contracts.
