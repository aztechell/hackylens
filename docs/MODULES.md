# Modules

## External link and vision results

`services/external_link_protocol.*` owns the versioned transport-neutral wire
codec. `services/external_link_service.*` dispatches it over external UART1 or
I2C0 without touching the UART3 debug console. `services/vision_result_service.*`
is the shared producer/consumer boundary for fixed BLOCK/ARROW results. Physical
pin and peripheral operations stay in board/HAL. See
`docs/EXTERNAL_LINK_PROTOCOL.md` for the public wire contract.

Default enabled apps: TERMINAL, CAMERA, QR-CAMERA, FACE DETECT, FILES, BUTTONS, PONG, SETTINGS, SLEEP.

Compile-time app flags are generated into `hk_config.h` by `tools/build_firmware.py`. The app registry lives in `apps/app_registry.c`. `--disable-app pong`, `--disable-app terminal`, and `--disable-app face-detect` each omit the corresponding complete feature directory. Shared camera, FAT32, KPU/DVP HAL, logging, debug-console, UART, and settings facilities remain available when required by other features.

Key public interfaces:

- `core/hk_app.h`, `core/hk_app_registry.h`, and `core/hk_screen.h` for app metadata, lookup, and screen model.
- `core/pixel_source.h` for a neutral pixel-reader contract.
- `runtime/hk_main.h` and `runtime/firmware_startup.h` for the platform loop and startup composition.
- `services/settings_persistence.h` and `services/settings_lights.h` for settings load and application.
- `services/debug_console_service.h` for narrow debug UART read/write access.
- `services/debug_screenshot_stream.h` and `services/screenshot_source.h` for screenshot UART transport and LCD shadow sourcing.
- `drivers/camera_stream.h` for the IRQ-driven two-slot camera stream and its explicit frame-lease contract; SDK interrupt details remain private to `hal/hal_dvp.c`.
- `apps/face_detect/face_detect_app.h` is the sole public FACE DETECT interface; its private detector, storage, controller, view, configuration, and types remain inside the module. The KPU model is read from `/hackylens.kmodels/detect.kmodel` on the SD card.
- `drivers/hk_lcd.h` for the synchronous full-frame RGB565-BE surface lease; UI composes into the existing LCD shadow before a single driver-owned SPI present.
- `storage/screenshot_bmp.h` for BMP encoding, `storage/screenshot_writer.h` for persistence, and focused FAT32/file headers for storage operations.
- `storage/image_viewer.h` for BMP/PNG/PPM/RAW and streaming animated GIF87a/GIF89a viewing. GIF playback supports palettes, transparency, interlace, disposal, pause/resume, bulk sub-block reads, and a 1600x1200 logical-canvas limit without loading the complete file into RAM. FILES decodes FAT long names from UTF-16 to UTF-8, renders Russian Cyrillic, and sorts entries by FAT modification time with newest entries first.
- `ui/hk_ui.h` and per-screen view headers for UI rendering.

Private headers are allowed only within their subsystem boundary; they are not compatibility facades or new global contracts.
