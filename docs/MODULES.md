# Modules

> HackyLens v0.4 is a layered K210 reference firmware and MicroPython technology
> preview.

## External link and vision results

`services/external_link_protocol.*` owns the versioned transport-neutral wire
codec. `services/external_link_service.*` dispatches it over external UART1 or
I2C0 without touching the UART3 debug console. `services/vision_result_service.*`
is the shared producer/consumer boundary for fixed BLOCK/ARROW results. Physical
pin and peripheral operations stay in board/HAL. See
`docs/EXTERNAL_LINK_PROTOCOL.md` for the public wire contract.

Default enabled apps: TERMINAL, CAMERA, QR-CAMERA, FACE DETECT, APRILTAG,
OBJECT DETECT, MICROPYTHON, FILES, BUTTONS, PONG, SETTINGS, SLEEP.

## Typed services and app runtime

`firmware/include/hackylens/capability/` exposes Time, Input, Lights, Display
and External Link. Each has one immutable platform binding. Time and Input have
board lifetime; Lights, Display and External Link use stable sessions and local
conflict checks. Native code and MicroPython use the same implementations.
There is no generic owner table, broker or generated provider inventory.

`firmware/src/runtime/` owns the foreground start/event/render/stop lifecycle.
Scoped cleanup retires all native sessions with one original teardown deadline,
even after an earlier error. Persistent settings and MicroPython worker sessions
retain their own lifetimes. Shared fake/K210 suites check service contracts.
See [App Runtime](spec/APP_RUNTIME.md) and [typed services](spec/CAPABILITY_API.md).

Compile-time app flags are generated into `hk_config.h` by
`tools/build_firmware.py`. Canonical manifests generate one immutable registry
in `build/generated/app_registry/registry.*` during the firmware build; the
app-neutral lookup and dispatch implementation lives in `core/hk_app_registry.c`.
Every `--disable-app <name>` omits the corresponding complete
`apps/<feature>/` directory. Disabling QR-CAMERA also omits `quirc`; disabling
APRILTAG omits its vendored detector core and TAG36H11 table. The shared core-1
executor remains when APRILTAG or MICROPYTHON requires it. Shared camera sources
are retained only while at least one camera consumer is enabled; the shared
planar AI input requires FACE or OBJECT.

Key interfaces (firmware-private unless exported by the SDK):

- `hackylens/capability/common.h` for results, deadlines and buffer views,
  plus the five typed service headers for hardware operations.
- `core/ai_model_types.h` and `services/ai_model_runtime.h` for model metadata
  and the instance-based load/run/stop/unload API. `storage/ai_model_storage.h`
  and `platforms/k210/hal/hal_kpu.h` are implementation boundaries used by the runtime, not
  feature APIs. See `docs/AI_MODELS.md` for the SD manifest and conversion lab.
- `core/hk_app.h`, `core/hk_app_registry.h`, and `core/hk_screen.h` for private generated descriptor and typed-entry metadata, stable autostart lookup, and the screen model. Canonical registry enumeration is the only source of enabled autostart choices and is independent of menu visibility/order; the generated reserved-ID set governs persistence even for disabled apps. SETTINGS and SLEEP have no autostart ID.
- `apps/camera/camera_app.h`, `apps/qr_camera/qr_camera_app.h`, `apps/files/files_app.h`, `apps/buttons/buttons_app.h`, `apps/settings/settings_app.h`, and `apps/sleep/sleep_app.h` are the sole public contracts for the newly isolated modules. Their private controllers, adapters, decoders, views, and configuration are not shared APIs.
- `controllers/settings_menu_controller.h` for reusable instance-based settings menus. Owners supply item descriptors and callbacks; the component owns navigation, edit/cycle interaction, static or dynamic choices, partial redraw, repeat, and commit notification but never persistence or application lifecycle. CAMERA, QR, APRILTAG, OBJECT DETECT, and system SETTINGS are current consumers.
- `core/pixel_source.h` for a neutral pixel-reader contract.
- `runtime/hk_main.h` and `runtime/firmware_startup.h` for the platform loop and startup composition.
- `services/settings_persistence.h` and `services/settings_lights.h` for settings load and application.
- `services/debug_console_service.h` for narrow debug UART read/write access.
- `services/debug_screenshot_stream.h` and `services/screenshot_source.h` for screenshot UART transport and LCD shadow sourcing.
- `drivers/camera_stream.h` for the IRQ-driven two-slot camera stream and its explicit frame-lease contract; SDK interrupt details remain private to `platforms/k210/hal/hal_dvp.c`.
- `apps/face_detect/face_detect_app.h` is the sole public FACE DETECT interface; its private YOLO decoder, controller, DVP adapter, view, configuration, and types remain inside the module. The KPU model is read from `/hackylens.kmodels/detect.kmodel` through the shared AI runtime.
- `apps/apriltag/apriltag_app.h` is the sole public APRILTAG interface. The module detects TAG36H11 markers on a core-1 worker, reports native IDs `0..586`, and owns its hold-OK settings lifecycle and descriptor adapter, central selection crosshair, persistent selected-ID bitmap, and `ALL/SELECTED` publication filter. Unselected blocks are green and selected IDs are yellow; the numeric ID stays inside each block.
- `apps/object_detect/object_detect_app.h` is the sole public OBJECT DETECT interface. The module owns VOC20 decoding, class labels, overlays, settings, persistence, and diagnostics. Its SD package is `/hackylens.kmodels/object20/`.
- `services/camera_ai_input.h` owns the single aligned planar DVP/KPU input and exact frame-boundary handoff shared by FACE and OBJECT. `services/core1_executor.h` owns APRILTAG's reusable core-1 job slot.
- `services/camera_session_preferences.h` supplies optional per-session FPS and LED/RGB overrides. APRILTAG and OBJECT use independent values; CAMERA and QR continue to read their normal persisted profile after the override is cleared.
- Settings storage v5 keeps APRILTAG's original 80-byte app block and OBJECT's eight-byte extension, widens stable autostart identity to uint16, and keeps the aligned record at 124 bytes. The loader accepts v1/v2/v3/v4; v4 IDs are zero-extended without changing previous settings.
- `ui/display_binding.h` for the private native-view binding to a typed Display
  BASE handle. `capabilities/display.c` owns the public dispatch boundary,
  `platforms/k210/capabilities/display_adapter.c` owns plane state and bounded
  composition, and `drivers/lcd_st7789_transport.h` is raw panel transport only.
- `storage/screenshot_bmp.h` for BMP encoding, `storage/screenshot_writer.h` for persistence, and focused FAT32/file headers for storage operations.
- `storage/file_mount.h` and `storage/file_dir_scan.h` for neutral FAT mount and directory queries. Browser lists and image viewing are private FILES APIs.
- `storage/sd_card.h` is the permanent private raw-block boundary; no feature
  app includes an SD driver header. `services/frame_pool.*` owns the two fixed
  camera slots and the mutually exclusive generation-checked scratch workspace;
  `services/frame_workspace.h` exposes only its internal borrow/release view.
- `apps/files/image_viewer.h` is private to FILES and supports BMP/PNG/PPM/RAW plus streaming animated GIF87a/GIF89a. GIF playback supports palettes, transparency, interlace, disposal, pause/resume, bulk sub-block reads, and a 1600x1200 logical-canvas limit without loading the complete file into RAM. FILES decodes FAT long names from UTF-16 to UTF-8, renders Russian Cyrillic, and sorts entries by FAT modification time with newest entries first.
- `ui/hk_ui.h` and per-screen view headers for UI rendering.

Private headers are allowed only within their subsystem boundary; they are not compatibility facades or new global contracts.

`tools/architecture_layers.toml` is the explicit layer classification consumed
by architecture guard v2. The guard checks repository sources, forwarding and
resolved symlink targets, generated compiler dependency files, undefined object
symbols, static typed bindings, and identical provider objects in
full and MicroPython-disabled profiles. The `public-capability` layer contains
the public headers and the five common lifecycle frontends; provider bindings,
private state machines, and K210 adapters remain `capability-implementation`.
The map also classifies `sdk`, `app-runtime`, `manifest`,
and `generated-app-registry` layers. SDK headers may depend on
`public-capability` only; runtime and generated descriptors cannot acquire a
board/HAL/driver policy edge. The rules use path families and contain no
allowlist for concrete apps.
