# App Lifecycle

Apps are described by `hk_app_t`: `id`, `title`, `screen`, `enter`, optional `exit`, optional `tick`, optional `handle_input`, and optional `draw_icon`.

`runtime/hk_main.c` runs the firmware superloop: it processes debug input, polls buttons, dispatches shell input, ticks the menu and active app, runs the system tick, then sleeps for the configured camera or non-camera interval. `SCREEN_MENU` opens the selected registry entry through its `enter` callback. `SCREEN_CAMERA_SETTINGS` remains a service screen owned by CAMERA/QR-CAMERA rather than a top-level app.

Screen state is exposed through `core/hk_screen.h`. Apps and controllers receive `hk_input_snapshot_t` from the runtime loop and must not access input driver state directly.

App-specific code may live in a self-contained `apps/<feature>/` directory. Pong and Terminal use this layout; `apps/app_registry.c` includes only their public `pong_app.h` and `terminal_app.h` entry points.
