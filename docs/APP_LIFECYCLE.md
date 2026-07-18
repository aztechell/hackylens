# App Lifecycle

Apps are described by `hk_app_t`: `id`, `title`, `screen`, `enter`, optional `exit`, optional `tick`, optional `handle_input`, optional `draw_icon`, optional `background_tick`, optional `handle_debug_command`, and optional `debug_help`. Registry entries use designated initializers so optional fields are independent of structure order.

`runtime/hk_main.c` runs the firmware superloop: it processes debug input, polls buttons, dispatches shell input, ticks the menu and active app, runs the system tick, then sleeps for the configured camera or non-camera interval. `SCREEN_MENU` opens the selected registry entry through its `enter` callback. `SCREEN_CAMERA_SETTINGS` remains a service screen owned by CAMERA/QR-CAMERA rather than a top-level app.

Screen state is exposed through `core/hk_screen.h`. Apps and controllers receive `hk_input_snapshot_t` from the runtime loop and must not access input driver state directly.

App-specific code may live in a self-contained `apps/<feature>/` directory. Pong, Terminal, and FACE DETECT use this layout; `apps/app_registry.c` includes only their public app entry points. The registry dispatches background ticks and debug commands generically and contributes each enabled app's `debug_help` token to `HKHELP`.

FACE DETECT starts the shared camera session and loads `/hackylens.kmodels/detect.kmodel` from the SD card on entry. Its `exit` callback requests model unload exactly once. If KPU inference is still active, the module's `background_tick` completes the deferred unload after navigation has already returned to the menu; BACK handlers do not unload the model directly.
