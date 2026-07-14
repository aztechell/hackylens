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

## Feature modules

`apps/pong/` and `apps/terminal/` are self-contained app modules. Their app entry points, controllers, views, configuration, state, and menu icons live inside each feature directory. The app registry is the sole shared-layer connection to either module; the corresponding feature flag and build manifest select each module as a unit. Feature code may use core lifecycle, input, LCD/UI, logging, settings, and menu/back-navigation contracts, while drivers, HAL, runtime, menu, and shared UI remain in the layered architecture. Other apps, including FACE DETECT, use the shared layered directories and are still selected through the same compile-time registry.

Terminal owns its bounded line ring, viewport, scrolling, font geometry, and log-sink lifecycle. The shared logging service exposes only a generic optional sink and remains independent of Terminal. Font selection is stored in reserved feature bits of the existing version-1 settings payload, so old records and erased flash continue to decode as `TERMINAL_FONT_NORMAL` without changing record size or storage version.

## Architecture freeze

The current Layered Architecture is complete. `controllers` may continue to use `hal_time` as an embedded shortcut; `runtime` is the composition root and may connect lower layers. Future file moves are allowed only to fix a concrete bug, add a platform, or introduce a subsystem. Refactoring solely to raise a perceived architecture-cleanliness percentage is prohibited.
