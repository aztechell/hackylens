# Porting a MicroPython prototype to C

Prototype against `hackylens`, then move the application logic into a native
app using the public App SDK. The C build selects providers from the target
board. Keep board wiring and drivers behind the SDK. The same app source and
manifest build for every target that supplies the declared services.

## Match the calls and data

| MicroPython | Public C SDK | What to carry over |
|---|---|---|
| `ticks_ms()`, `ticks_us()`, `sleep_ms()` | Time from `hk_app_context_time`, `hk_time_now_us`, Timer events | Schedule the next step with monotonic time. Return control to the runtime between steps. |
| `button()`, `buttons()` | `HK_APP_EVENT_INPUT`, `hk_input_get_state` | Use `pressed` for a discrete action and `state` for a hold. Maix Cube exposes BACK through POW as a finite pulse. |
| `display_clear`, `display_rects`, `display_frame`, `display_text` | `hk_app_context_request_render`, then `hk_app_surface_*` in `render` | Keep RGB565 values and the draw order. Use `hk_app_surface_get_info` for the target's dimensions. The runtime submits the frame. |
| LED, RGB and illumination | `hk_app_context_lights`, `hk_lights_*` | Claim the channels you use and check the requested channel is supported. |
| External UART and I2C | `hk_app_context_external_link`, `hk_external_link_*` | Keep baud rates, addresses and byte formats. The foreground app borrows the connector and the default protocol resumes after teardown. |
| `accelerometer_read()` | `hk_app_context_accelerometer`, `hk_accelerometer_read` | Keep the signed axes in mg. Check availability for boards without this sensor. |
| `camera_open`, `camera_frame`, `camera_release`, `camera_close` | `hk_app_camera_open`, `hk_app_camera_acquire`, `hk_app_camera_release`, `hk_app_camera_close` | Python `None` while waiting for a frame maps to `HK_PENDING`. Retain at most one lease. |
| `camera_find_color`, `camera_present` | `hk_app_camera_find_color`, `hk_app_camera_present` | Copy the six inclusive RGB565 thresholds, region and minimum component size. Both APIs use logical coordinates after mounting rotation. C returns zero pixels for no match. |
| `camera_scan` | `hk_app_camera_scan_begin`, `hk_app_camera_scan_poll` | Use QR or AprilTag as the source. Keep the frame until completion. Read QR payloads as bytes with an explicit length. |
| `camera_model_open`, `camera_detect`, `camera_model_close` | `hk_app_camera_model_open_step`, `hk_app_camera_detect_begin`, `hk_app_camera_detect_poll`, `hk_app_camera_model_close` | Use the same model package on the SD card. Carry the source, rectangle and confidence semantics over unchanged. |
| `vision_results()` | `hk_vision_read` | Copy the latest native detector publication into a `vision_result_snapshot_t`. This is independent of a leased camera frame. |
| `camera_save` | `hk_app_camera_save_begin`, `hk_app_camera_save_step` | Keep the frame until the BMP24 is published. Use the same FAT 8.3 name under `/PYDATA`. |
| `sd_begin`, `sd_append`, `sd_finish`, `sd_cancel` | `hk_app_data_begin`, `hk_app_data_write`, `hk_app_data_finish`, `hk_app_data_cancel` | Keep the same block and advance by the accepted byte count. Retry a pending step on a later tick while continuing to receive input. |
| `audio_available`, `audio_play`, `audio_record_into` | `hk_app_audio_info`, `hk_app_audio_play_begin`, `hk_app_audio_record_begin`, `hk_app_audio_poll` | Use signed 16-bit little-endian mono PCM. Query the sample rate and size limit. Keep the buffer in app or static storage until completion. |

## Translate the execution model

1. Keep pure calculations independent of I/O. Port thresholds, state
   transitions and expected output first, then compare them on the same inputs.
2. Move initialization into `start` and persistent variables into the app state
   slot. In `event`, handle input and perform one bounded step of outstanding
   work on each Timer event. On `HK_PENDING`, retain the state and return
   `HK_OK` from the callback so the next event can resume the operation.
   Configure `tick_budget_ms` to allow for storage I/O and retries. The color
   tracker requests a timer every 10 ms and gives a callback up to 1000 ms to
   complete a storage step.
3. Use `render` for app-owned drawing. For a camera preview drawn by
   `hk_app_camera_present`, set `render` to `NULL` and present from a Timer event.
4. Convert exceptions into explicit status branches. Handle BACK with
   `hk_app_context_request_close`. Release an acquired frame on every terminal
   path. The runtime also cancels and retires app-owned service claims at stop.
5. Declare required services in `app.toml`. For a mandatory QR scan, include
   `camera` and `qr-decoder`. AprilTag uses `apriltag-decoder`. Face and object
   detection use `face-detector` or `object-detector` with `camera` and `sd-card`.
   A mandatory PCM feature declares `audio`. To offer PCM as an extra, query
   `hk_app_audio_info` and handle `HK_ERR_FEATURE_UNAVAILABLE`.
6. To list the app in the menu, choose a free `menu_order` and set
   `menu_icon = "label"`. The shell draws the tile from the app ID.

HMPY, imports, the Python heap and USERFS script files belong to the prototype
workflow. The native app is linked into the image for the selected board.
Testing model loading still requires the corresponding model package and a
compatible SD card.

## Run both versions of the color tracker

Both implementations capture a frame, find the largest region using the same
threshold and pixel count, show an outline and save a BMP plus a text result
on OK. `COLOR TRACKER` is a hidden reference app. Open it with `HKAPP
color-tracker` on the debug UART. `HKCOLORINFO` reports the last frame and save
status. `HKCOLORSAVE` requests the same operation as OK. BACK returns to the menu.

- [MicroPython prototype](../examples/color_tracker.py)
- [C source](../firmware/src/apps/color_tracker/color_tracker_app.c)
- [Native app manifest](../firmware/src/apps/color_tracker/app.toml)

```sh
python tools/build_firmware.py full --board huskylens-sen0305 --require-app color-tracker
python tools/build_firmware.py full --board sipeed-maix-cube --require-app color-tracker
```

Run the prototype from MICRO-PYTHON or HMPY on the full build. To check that your
own native app is independent of the VM, rebuild with `--disable-app micropython`.
Use `--require-app` to make a missing target service an error. On each target,
verify the app starts, processes input, releases its resources and can reopen
after a normal exit or an interrupted operation. Use the same capture or sample
data when comparing outputs across languages.

The [UART Logger study](UART_LOGGER_STUDY.md) exercises this workflow with a
continuous UART stream and incremental SD writes.
The [Object Gate study](OBJECT_GATE_STUDY.md) compares both paths with a
shared model on the K210 KPU and the same decoded detections.
