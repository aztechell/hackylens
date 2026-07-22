# App Lifecycle

Apps are described by `hk_app_t`: `id`, `title`, `screen`, `enter`, optional `exit`, optional `tick`, optional `handle_input`, optional `draw_icon`, optional `background_tick`, optional `handle_debug_command`, and optional `debug_help`. Registry entries use designated initializers so optional fields are independent of structure order.

`runtime/hk_main.c` runs the firmware superloop: it processes debug input, polls buttons, dispatches shell input, ticks the menu and active app, runs the system tick, then sleeps for the configured camera or non-camera interval. `SCREEN_MENU` opens the selected registry entry through its `enter` callback. `SCREEN_CAMERA_SETTINGS` remains a service screen owned by CAMERA/QR-CAMERA rather than a top-level app.

Screen state is exposed through `core/hk_screen.h`. Apps and controllers receive `hk_input_snapshot_t` from the runtime loop and must not access input driver state directly.

Reusable settings menus are neutral child sessions rather than screens or applications. `settings_menu` reports a close request to its owner; the owner decides whether BACK resumes a camera, returns to a game, or changes screens. Values, persistence, and immediate hardware effects are provided through owner callbacks. Ending edit mode through OK, BACK, or forced session close emits the same commit callback.

CAMERA and QR freeze capture before opening different descriptor tables in one camera settings controller. BACK closes the child session and preserves the existing camera resume, QR resume, or size-reinitialization path. System SETTINGS uses edit-on-OK rows; BACK first commits and leaves edit mode, while a second BACK exits. Its app `exit` callback closes the session so `HKMENU` also commits an active edit.

App-specific code may live in a self-contained `apps/<feature>/` directory. Pong, Terminal, FACE DETECT, and APRILTAG use this layout; `apps/app_registry.c` includes only their public app entry points. The registry dispatches background ticks and debug commands generically and contributes each enabled app's `debug_help` token to `HKHELP`.

FACE DETECT starts the shared camera session and loads `/hackylens.kmodels/detect.kmodel` from the SD card on entry. Its `exit` callback requests model unload exactly once. If KPU inference is still active, the module's `background_tick` completes the deferred unload after navigation has already returned to the menu; BACK handlers do not unload the model directly.

APRILTAG starts a persistent core-1 worker before entering the shared camera in fixed 320x240 analysis mode. Its frame consumer downsamples only when the private luma handoff is free; frames arriving during detection are discarded so the worker never drains stale queued frames and always accepts the next newest preview. Completed native IDs are atomically published as BLOCK results. The previous stable overlay is composed into the next LCD frame and survives one isolated detection miss, avoiding frame-transfer flicker.

Short OK toggles the ID whose block contains the fixed center crosshair; a 700 ms hold freezes capture and opens a shared `settings_menu` session configured by APRILTAG descriptors while remaining under the app lifecycle. Entering settings clears the external result snapshot. BACK reports a neutral close request; APRILTAG then resumes the camera stream, ignores the held-OK release, skips pre-settings detector results, and waits for a new frame. Live BACK remains on core 0 and returns immediately; `exit` stops any paused camera path, invalidates pending results, clears camera session overrides, and requests detector destruction without waiting for an in-flight detection. Selected IDs are native TAG36H11 IDs rather than original HUSKYLENS learned slots.
