# Modules

## External link and vision results

`services/external_link_protocol.*` owns the versioned transport-neutral wire
codec. `services/external_link_service.*` dispatches it over external UART1 or
I2C0 without touching the UART3 debug console. `services/vision_result_service.*`
is the shared producer/consumer boundary for fixed BLOCK/ARROW results. Physical
pin and peripheral operations stay in board/HAL. See
`docs/EXTERNAL_LINK_PROTOCOL.md` for the public wire contract.

Default enabled apps: TERMINAL, CAMERA, QR-CAMERA, FACE DETECT, APRILTAG, FILES, BUTTONS, PONG, SETTINGS, SLEEP.

Compile-time app flags are generated into `hk_config.h` by `tools/build_firmware.py`. The app registry lives in `apps/app_registry.c`. Every `--disable-app <name>` omits the corresponding complete `apps/<feature>/` directory. Disabling QR-CAMERA also omits `quirc`; disabling APRILTAG omits its vendored detector core and TAG36H11 table. Shared camera sources are retained only while at least one camera consumer is enabled.

Key public interfaces:

- `core/hk_app.h`, `core/hk_app_registry.h`, and `core/hk_screen.h` for app metadata, stable autostart IDs, lookup, and screen model. Registry enumeration is the only source of enabled autostart choices; SETTINGS and SLEEP have no autostart ID.
- `apps/camera/camera_app.h`, `apps/qr_camera/qr_camera_app.h`, `apps/files/files_app.h`, `apps/buttons/buttons_app.h`, `apps/settings/settings_app.h`, and `apps/sleep/sleep_app.h` are the sole public contracts for the newly isolated modules. Their private controllers, adapters, decoders, views, and configuration are not shared APIs.
- `controllers/settings_menu_controller.h` for reusable instance-based settings menus. Owners supply item descriptors and callbacks; the component owns navigation, edit/cycle interaction, static or dynamic choices, partial redraw, repeat, and commit notification but never persistence or application lifecycle. CAMERA, QR, APRILTAG, and system SETTINGS are current consumers.
- `core/pixel_source.h` for a neutral pixel-reader contract.
- `runtime/hk_main.h` and `runtime/firmware_startup.h` for the platform loop and startup composition.
- `services/settings_persistence.h` and `services/settings_lights.h` for settings load and application.
- `services/debug_console_service.h` for narrow debug UART read/write access.
- `services/debug_screenshot_stream.h` and `services/screenshot_source.h` for screenshot UART transport and LCD shadow sourcing.
- `drivers/camera_stream.h` for the IRQ-driven two-slot camera stream and its explicit frame-lease contract; SDK interrupt details remain private to `hal/hal_dvp.c`.
- `apps/face_detect/face_detect_app.h` is the sole public FACE DETECT interface; its private detector, storage, controller, view, configuration, and types remain inside the module. The KPU model is read from `/hackylens.kmodels/detect.kmodel` on the SD card.
- `apps/apriltag/apriltag_app.h` is the sole public APRILTAG interface. The module detects TAG36H11 markers on a core-1 worker, reports native IDs `0..586`, and owns its hold-OK settings lifecycle and descriptor adapter, central selection crosshair, persistent selected-ID bitmap, and `ALL/SELECTED` publication filter. Unselected blocks are green and selected IDs are yellow; the numeric ID stays inside each block.
- `services/camera_session_preferences.h` supplies optional per-session FPS and LED/RGB overrides. APRILTAG uses it for independent values; CAMERA and QR continue to read their normal persisted profile after the override is cleared.
- Settings storage v3 keeps the fixed opaque 80-byte app block and appends one autostart ID byte. The loader accepts v1/v2, defaults their autostart to OFF, and preserves CAMERA, external-link, and APRILTAG data.
- `drivers/hk_lcd.h` for the synchronous full-frame RGB565-BE surface lease; UI composes into the existing LCD shadow before a single driver-owned SPI present.
- `storage/screenshot_bmp.h` for BMP encoding, `storage/screenshot_writer.h` for persistence, and focused FAT32/file headers for storage operations.
- `storage/file_mount.h` and `storage/file_dir_scan.h` for neutral FAT mount and directory queries. Browser lists and image viewing are private FILES APIs.
- `apps/files/image_viewer.h` is private to FILES and supports BMP/PNG/PPM/RAW plus streaming animated GIF87a/GIF89a. GIF playback supports palettes, transparency, interlace, disposal, pause/resume, bulk sub-block reads, and a 1600x1200 logical-canvas limit without loading the complete file into RAM. FILES decodes FAT long names from UTF-16 to UTF-8, renders Russian Cyrillic, and sorts entries by FAT modification time with newest entries first.
- `ui/hk_ui.h` and per-screen view headers for UI rendering.

Private headers are allowed only within their subsystem boundary; they are not compatibility facades or new global contracts.
