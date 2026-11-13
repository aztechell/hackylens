---
contract-id: hackylens.micropython-api
owner: micropython-runtime
version: 1.5.0
stability: experimental
api-major: 1
---

# HackyLens MicroPython API v1

HackyLens embeds upstream MicroPython inside the main firmware. A program runs
on K210 core 1 with a private 128 KiB GC heap and a 24 KiB conservative stack
limit. Core 0 keeps the menu, serial protocol, display, flash, and hardware
services responsive. HMPY runs default to 30 seconds and accept up to 300
seconds. Runs started on the device continue until completion, an error, or a
stop request.

The native **MICRO-PYTHON** app is a script manager. It lists USERFS `.py`
files, previews source without loading the whole file, runs the selected file,
shows scrollable output, changes startup selection, and deletes only after an
explicit confirmation. Opening the app does not run the selected startup file.

For power-on launch, pin a script to the menu, then select it under
**SETTINGS → Startup**. It runs with the same lifecycle as the menu shortcut.
BACK at boot skips autostart, and a watchdog reset also skips the script.
The startup selection in MICRO-PYTHON remains available for the debug command.
That selection remains the HMPY/default program used when `RUN` omits a name.

Select a saved script in MICRO-PYTHON and choose **PIN TO MENU**. Up to eight
scripts appear after the built-in apps in the main menu. Select a shortcut and
press OK to run it with the PC disconnected. BACK stops the script and returns
to the shortcut after the runtime releases its resources. The IDE's STOP also
stops a local run when connected. Pin state survives reboot and re-upload of
the same file. Choose **UNPIN FROM MENU** to remove the shortcut. Deleting the
file removes its shortcut too.

Import the board module with:

```python
import hackylens as hl
```

## Buttons and time

| API | Result |
|---|---|
| `buttons()` | current debounced button bit mask from `hackylens.cap.input` |
| `button(mask)` | `True` if any bit in `mask` is pressed |
| `ticks_ms()` | monotonic milliseconds since boot |
| `sleep_ms(duration)` | cooperative sleep; `0..300000` ms |

Button constants are `BUTTON_LEFT = 1`, `BUTTON_OK = 2`,
`BUTTON_RIGHT = 4`, and `BUTTON_BACK = 8`. `sleep_ms()` checks both a remote
stop request and the run deadline at least every 5 ms.

## Display

SEN0305 has a 320 by 240 display and Maix Cube has a 240 by 240 display.
Colors are unsigned RGB565 values from `0x0000`
through `0xffff`.

| API | Meaning |
|---|---|
| `display_clear(color=0)` | fill the complete display |
| `display_text(x, y, text, foreground=0xffff, background=0)` | draw up to 256 encoded bytes |
| `display_rect(x, y, width, height, color, filled=False)` | draw an outline or filled rectangle |
| `display_size()` | return the selected display's `(width, height)` |
| `display_rects(data, count=None)` | stage up to 256 filled rectangles in one call, within the frame's remaining command capacity |
| `display_present()` | atomically make the currently staged display list visible |

`display_clear()`, `display_text()`, and `display_rect()` only stage a bounded
frame; the panel is updated by `display_present()`. `display_clear()` starts a
fresh staged frame, while a successful present replaces the previous Python
overlay. Coordinates must start on screen; rectangles and text are clipped at
the right and bottom edges. Invalid dimensions, oversized text, and a command
or text buffer overflow raise `ValueError`. Script cleanup removes the overlay
and restores the latest firmware-owned screen.

`display_rects()` takes a `bytes` or `bytearray` with five unsigned 16-bit
little-endian values per rectangle: `x`, `y`, `width`, `height`, and RGB565
`color`. With `count`, only the first `count` records are read. An invalid or
oversized batch leaves the previous staged commands intact. Both rectangle
functions use the same display clipping rules. A frame can stage up to 256
commands, including one for `display_clear()` and two per `display_text()`.

## Illumination and RGB LED

| API | Meaning |
|---|---|
| `led(brightness)` | white illumination LED, `0..100`; zero turns it off |
| `rgb(red, green, blue)` | RGB status LED, each channel `0..255`; all zero turns it off |

The script temporarily owns any light it changes. When the program completes,
is stopped, times out, or raises an exception, firmware reapplies the persisted
light settings.

## External UART

UART and I2C use the descriptor-selected HUSKYLENS external connector routes.
The script claims that connector on its first UART or I2C call; the normal
HackyLens external-link service is suspended until script cleanup.

| API | Meaning |
|---|---|
| `uart_init(baud=115200)` | select UART at `1200..2000000` baud |
| `uart_write(data)` | transmit `str` or `bytes`; returns the byte count |
| `uart_read(size=64)` | return up to `0..256` currently buffered bytes |

`uart_write()` transparently splits larger values into 256-byte service calls.
`uart_read()` is non-blocking and may return `b""`.

## External I2C

The v1 I2C binding is a 100 kHz controller with 7-bit addresses.

| API | Meaning |
|---|---|
| `i2c_write(address, data)` | write up to 256 bytes to address `1..127` |
| `i2c_read(address, size, prefix=b"")` | optional prefix write, then read `0..256` bytes |

An I2C transaction has a 100 ms hardware deadline. A NACK or bus error raises
`OSError`; a service deadline raises `RuntimeError`. The connector mode and
normal external-link service are restored after every run.

## Built-in audio

| API | Meaning |
|---|---|
| `audio_available()` | whether the build supports built-in audio |
| `audio_play(pcm)` | play a readable PCM buffer through the speaker |
| `audio_record_into(pcm)` | fill a writable PCM buffer from the microphone and return its byte count |

Maix Cube supports these operations. On SEN0305, `audio_available()` returns
`False` and playback or recording with a valid buffer raises `OSError`.

Audio buffers contain signed 16-bit little-endian mono PCM. Each call accepts an
even byte count from 2 through `AUDIO_MAX_BYTES` (16,000). `AUDIO_RATE_HZ` is
15,869 samples per second, giving about half a second per buffer. The call waits
for its transfer while firmware services continue to run. Stopping a script
cancels the transfer. See the [speaker and microphone demo](../examples/audio_demo.py).
Playback uses the volume saved by RECORDER or FILES. An active recording or
file playback holds the audio device until it completes or stops.

## Built-in accelerometer

`hackylens.accelerometer_available()` reports whether the board provides an
accelerometer. `hackylens.acceleration_mg()` returns signed `(x, y, z)` values
in milli-g, including gravity, in native sensor-axis order. It returns `None`
while the sensor starts and raises `OSError` on unsupported boards or read
failure. Maix Cube has this capability. SEN0305 reports it as unavailable.

## Vision results

`hackylens.vision_results()` returns a copy of the latest publication from
FACE DETECT, QR-CAMERA, APRILTAG, OBJECT DETECT or FACE RECOG on either board. It uses the
same snapshot as the native C SDK's `hk_vision_read()`.

```python
import hackylens as hl

result = hl.vision_results()
for kind, flags, id, x0, y0, x1, y1, confidence in result["items"]:
    if kind == hl.VISION_BLOCK:
        print(id, x0, y0, x1, y1, confidence)
if result["source"] == hl.VISION_QR and result["items"]:
    print(result["qr_payload"])
```

- `frame_id` identifies a publication and advances on updates and clears.
  Multiple reads between updates return the same ID.
- `source` is one of `VISION_NONE`, `VISION_FACE`, `VISION_QR`,
  `VISION_APRILTAG`, `VISION_OBJECT_DETECT`, `VISION_FACE_RECOGNITION` or the
  reserved `VISION_USER`.
- `width` and `height` give the source canvas in pixels. `items` contains up
  to eight tuples in the order shown above. For `VISION_BLOCK`, the two
  points bound the detection. For `VISION_ARROW`, they give its endpoints.
  IDs are source-specific. AprilTag uses `flags` for the Hamming distance.
  `confidence` is on a 0–1000 scale with source-specific meaning.
  [FACE RECOG](FACE_RECOGNITION.md) uses ID zero for unknown faces and the
  nonnegative cosine similarity for confidence.
- `qr_payload` is `bytes`. A decoded QR result carries one bounding block
  and up to 383 payload bytes, including any embedded zero bytes. The block
  has ID and confidence set to zero. Other publications carry `b""`.

Before a detector publishes or after the publishing app exits, the snapshot
has source `VISION_NONE`, dimensions zero and empty items. A completed scan
without detections retains its source and dimensions and has empty items.
An IDE-run script can read the active native app's results while that app stays
open. QR and AprilTag share core 1 with MicroPython. Read native results with a
short script so the detector can use core 1 again after the script ends. Use
`camera_scan()` to decode a frame captured by the script itself.

## Camera and color tracking

The camera API uses a QVGA RGB565 stream on SEN0305 and Maix Cube. Start
the script from the main menu via HackyLens Code or run a saved script
in MICRO-PYTHON. A native app with the foreground blocks camera open.
Use `camera_present()` for drawing during capture. A script can start
a regular display batch after closing the camera. A regular display batch
keeps the frame workspace reserved for the rest of that run.

| Function | Behavior |
| --- | --- |
| `camera_open()` | Claims the camera for the run. Raises `RuntimeError` if busy and `OSError` on initialization failure. |
| `camera_frame()` | Returns `(lease, width, height, sequence)` or `None` while a frame is pending. The run can hold one frame at a time. |
| `camera_find_color(lease, threshold, roi=None, min_pixels=8)` | Returns the largest four-connected component as `(x, y, width, height, pixels)`, or `None` if no component reaches the minimum. |
| `camera_present(lease, box=None, color=0x07e0)` | Fits the entire frame to the LCD and optionally outlines a rectangle in the given RGB565 color. |
| `camera_release(lease)` | Returns the frame slot to capture. |
| `camera_close()` | Stops capture, releases any held frame, and restores the underlying UI. |

The lease refers to the existing camera buffer. Frame pixels stay in
firmware memory. Pass optional arguments by position. ROI, box, and result rectangles use `(x, y, width, height)`
in frame pixels after board rotation, with the origin at the upper left.
Release the frame promptly so capture can reuse the slot. Using a released
lease raises `RuntimeError`. VM teardown closes the camera on completion,
STOP, or an uncaught exception.

In HackyLens Code, open Camera and take an RGB565 snapshot with the object in
the lighting where you will run the tracker. Drag across a representative
patch of the object and copy the threshold into the [color tracker example](../examples/color_tracker.py).
The six inclusive RGB565 bins are `(r_min, r_max, g_min, g_max, b_min, b_max)`.
Red and blue range from 0 to 31 and green ranges from 0 to 63. The live preview
uses RGB332 and produces coarser thresholds. Run the script to see the box on
the board and the coordinates and matched pixel count in the console. Pin the
saved script to run it from the main menu. Press OK during the run to save the
current frame and detection result to microSD. Press BACK to stop.

## microSD output

| Function | Behavior |
| --- | --- |
| `camera_save(lease, name)` | Saves the leased frame as a 24-bit BMP in its logical orientation. Returns `None` on success. |
| `sd_write(name, data)` | Creates a file from a bytes-like object and returns the number of bytes written. |

Both functions write inside `/PYDATA` on a FAT32 microSD card. The firmware
creates the directory when needed. Use a filename with a 1–8 character base
and a 1–3 character extension. Letters, digits, underscores and hyphens are
allowed. `camera_save` requires `.BMP`. The firmware reserves `.TMP` for
in-progress writes. Filename collisions are case-insensitive. Use a new name
for each save.

An invalid name raises `ValueError`. A stale frame lease raises `RuntimeError`.
An existing file, missing card, full volume or write failure raises `OSError`.
The firmware publishes the final name after writing all bytes. BACK and STOP
cancel a write that is still in progress. If the card is removed or the device
loses power during a save, a `.TMP` file can remain. Open finished images in
FILES or read the card on a computer.

## KPU detectors

Open the camera, then call `camera_model_open(source)` with `VISION_FACE`
or `VISION_OBJECT_DETECT`. The firmware loads the same SD model and uses the
same detector as the corresponding native app. FACE uses
`/hackylens.kmodels/detect.kmodel`. OBJECT DETECT uses the `object20` package.
The load reads the model in 16 KiB steps so the console can handle STOP.

`camera_detect(lease)` runs inference on the leased frame and returns a
dictionary with the same fields as `vision_results()`. Its `frame_id` is the
frame's `sequence`. Coordinates use the same logical orientation as
`camera_present()`. A result with no detections has empty `items`. FACE
uses the face index as the ID and reports confidence as 1000. OBJECT DETECT
returns the VOC class ID and confidence on the 0–1000 scale.

Pass an optional package name as the second argument of `camera_model_open`
to load from `/hackylens.kmodels/<package>/`. The name accepts 1–31 lowercase
ASCII letters, digits, hyphens, or underscores. The directory must contain
`model.kmodel`, `manifest.hkai`, and `labels.txt`. The model must match the
selected detector's KModel header, tensors, and YOLO decoding. OBJECT DETECT
uses the same anchors and VOC class order for all compatible packages. The
firmware checks the manifest against the actual model's CRC and size, declared
filenames, detector ID, and tensor contract before loading it into KPU.

Call `camera_model_close()` before opening another model. Closing the camera
or ending the Python run releases the model. An incompatible model or
manifest raises `ValueError`. Missing SD files or KPU errors raise `OSError`.
The [KPU detector example](../examples/kpu_detector.py) runs on both boards.

## QR and AprilTag

Call `camera_scan(lease, source)` with `VISION_QR` or `VISION_APRILTAG`
while holding a frame from `camera_frame()`. Both boards use the same
decoders as the native apps. QR tries the original and inverted images, then
uses a local luminance threshold as a fallback. It returns the first decoded
code. AprilTag scans for tag36h11 markers and reports up to
eight tags. The scan runs on the same core as the VM, so STOP takes effect
at a scan boundary. The run releases decoder memory on exit.

The result has the fields of `vision_results()`, with `frame_id` set to the
leased frame's sequence and half-open bounding boxes in the orientation of
`camera_present()`. QR returns raw bytes in `qr_payload`. AprilTag returns
the tag ID and Hamming distance in each item's `id` and `flags`. A scan
with no detections returns an empty `items` tuple. A stale lease raises
`RuntimeError`. See the [code scanner example](../examples/code_scanner.py)
and the [marker cards and USB check](../examples/markers/README.md).

## Example

```python
import hackylens as hl

hl.display_clear(0x0000)
hl.display_text(8, 8, "MicroPython ready", 0xffff, 0x0000)
hl.display_present()
hl.rgb(0, 64, 0)

last_ok = False
while not hl.button(hl.BUTTON_BACK):
    ok = hl.button(hl.BUTTON_OK)
    if ok:
        hl.led(60)
    else:
        hl.led(0)
    if ok != last_ok:
        hl.display_clear(0x0000)
        hl.display_text(8, 8, "MicroPython ready", 0xffff, 0x0000)
        if ok:
            hl.display_rect(8, 40, 80, 30, 0x07e0, True)
        hl.display_present()
        last_ok = ok
    hl.sleep_ms(20)
```

The [full-screen fireplace](../examples/fireplace.py) uses packed rectangles
to animate on both supported displays.

## Stop and cleanup contract

The VM checks stop/deadline hooks at Python branch and return points, in both
central iterator gateways used by native builtins such as `sum()` and
`min()/max()`, during cooperative sleep, and while waiting for a core-0 binding
call. A stop or deadline is delivered as `KeyboardInterrupt`; user code may
catch it briefly, but the request and deadline remain active. Every hardware
service call is bounded.

If core 1 has not finished two seconds after STOP, or five seconds after the
run deadline, core 0 starts WDT1 as a one-shot fatal fallback. The watchdog is
not armed during ordinary execution or flash mutation. Its hardware reset
cause suppresses firmware autostart for that boot; the native script manager
never starts a script on entry, and an explicit run remains available after
inspection. New
HMPY v1 clients can distinguish this recovery path through HELLO capability
`BOOT_FLAGS` and boot flag `WDT1_RECOVERY`; older v1 clients safely ignore the
formerly reserved byte.

After normal or cooperative execution, HackyLens deinitializes the VM, releases the
camera, restores lights and the external connector service, and reports the final state through the app and
HMPY protocol. The fixed heap and source buffers stay reserved for reuse by the
next run; no script allocation survives VM deinitialization. An uncaught
exception is reported as an error run and its traceback is sent through stdout.
The fatal watchdog path restores ownership by resetting the whole SoC because
asynchronously unwinding an arbitrary native C frame would be unsafe.
It has a dedicated `-wdtfi` firmware build and a two-phase read-only
baseline/post-reset acceptance procedure; that disruptive physical gate has not
yet been run. The production API cannot deliberately wedge core 1.
See [WDT1 hardware acceptance](WDT_HARDWARE_ACCEPTANCE.md).
