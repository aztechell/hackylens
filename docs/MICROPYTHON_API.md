---
contract-id: hackylens.micropython-api
owner: micropython-runtime
version: 1.1.0
stability: experimental
api-major: 1
---

# HackyLens MicroPython API v1

HackyLens embeds upstream MicroPython inside the main firmware. A program runs
on K210 core 1 with a private 128 KiB GC heap and a 24 KiB conservative stack
limit. Core 0 keeps the menu, serial protocol, display, flash, and hardware
services responsive. The default execution limit is 30 seconds and the maximum
requested limit is 300 seconds.

The native **MICRO-PYTHON** app is a script manager. It lists USERFS `.py`
files, previews source without loading the whole file, runs the selected file,
shows scrollable output, changes startup selection, and deletes only after an
explicit confirmation. Opening the app does not run the selected startup file.
That selection remains the HMPY/default program used when `RUN` omits a name.

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
open. QR and AprilTag share core 1 with MicroPython. Read their completed results
with a short script so the detector can use core 1 again after the script ends.

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

After normal or cooperative execution, HackyLens deinitializes the VM, restores lights and the
external connector service, and reports the final state through the app and
HMPY protocol. The fixed heap and source buffers stay reserved for reuse by the
next run; no script allocation survives VM deinitialization. An uncaught
exception is reported as an error run and its traceback is sent through stdout.
The fatal watchdog path restores ownership by resetting the whole SoC because
asynchronously unwinding an arbitrary native C frame would be unsafe.
It has a dedicated `-wdtfi` firmware build and a two-phase read-only
baseline/post-reset acceptance procedure; that disruptive physical gate has not
yet been run. The production API cannot deliberately wedge core 1.
See [WDT1 hardware acceptance](WDT_HARDWARE_ACCEPTANCE.md).

Raw camera, KPU control and SD/files access remain outside API v1.
