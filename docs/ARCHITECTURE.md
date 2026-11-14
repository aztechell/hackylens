# Architecture

HackyLens 0.8.0 is modular firmware built from common `firmware/src` layers,
the selected `boards/<id>` BSP, and the K210 HAL/startup under `platforms/k210`.

> HackyLens is a layered K210 reference firmware and MicroPython technology
> preview.

This is the current architecture and implementation status. [Roadmap](ROADMAP.md)
contains product priorities; [technical references](spec/README.md) describe
public interfaces. Historical plans and decisions remain in Git history.

The project aims to reuse feature logic across K210 boards, compose firmware
from isolated native apps, and share hardware services with MicroPython.
Portability and Python-to-native migration must be demonstrated with real
applications, measurements and physical ports. They are not established by the
number of interfaces or by compiling a second board descriptor.

```text
Native feature apps       MicroPython scripts
        |                         |
   App runtime              Python bindings
        +------------+------------+
                     |
              Typed services
                     |
              Drivers / K210 HAL
                     |
              Selected board BSP
```

Native apps are statically linked C/C++ code; MicroPython executes scripts.
There is no runtime native loader, manifest parser or provider discovery.
Native C and MicroPython read the same published detector snapshot through
`hk_vision_read()` and `hackylens.vision_results()`. The Python adapter copies
it through the existing core-0 mailbox. See the
[vision result API](MICROPYTHON_API.md#vision-results).

Scripts can also acquire a camera frame and run the shared FACE, OBJECT,
QR, or AprilTag implementation on it. Core 0 validates the frame lease and
stages the decoder input. QR and AprilTag decode on core 1 within the VM job.
The native apps schedule these decoders through the core-1 executor. See the
[MicroPython camera API](MICROPYTHON_API.md#camera-and-color-tracking).

## Runtime and hardware service ownership

All bundled apps use `start/event/render/stop`, one foreground switch and one
build-generated immutable registry. Render is optional and separate because
runtime owns Display transactions. App identity and persisted autostart IDs do
not depend on menu order. Existing bundled portable firmware services are not
therefore standalone public SDK APIs.

The launcher appends user-pinned script shortcuts after the native registry.
A shortcut opens the existing MICRO-PYTHON app with a saved program name. Pin
metadata lives in the USERFS file entry and follows atomic program replacement.

MicroPython can create files in `/PYDATA` on FAT32. The core-0 writer
publishes a temporary entry under its final name after the last sector is
written. It tracks the mount generation across calls. Cancellation unlinks the
temporary entry and frees its clusters incrementally. Camera saves use the
shared CAMERA encoder and the leased frame in the board's logical orientation.

Time and Input have immutable board-lifetime bindings. Input retains one sampler,
debounce state and event ring with independent reader cursors. Lights uses
stable channel sessions; Display uses stable plane sessions and transactions;
External Link owns exclusive mode, incremental operations and IRQ handoff.
Native code and MicroPython use the same production implementations. The old
generic broker, owner/grant/lease tables and inventory generator are removed.
Build-time service selection checks board resources/routes and app requirements;
absent optional services have null accessors.

Power has an immutable binding with explicit OFF/RESTART feature bits. The native
POWER app uses that public capability; the K210 provider performs bounded shutdown
preparation before AXP173 power-off or SoC reset. Preparation stops MicroPython,
flushes settings and unmounts userfs; failures prevent the hardware action.
Cube supports both operations, while SEN0305 exposes RESTART only. Sleep retains
its existing brightness/session lifecycle above the hardware provider.

Runtime prepares BASE before app start. Failed preparation and failed stop both
reach cleanup. Teardown creates one absolute deadline and passes it unchanged
to every scoped retirement, attempts all cleanup, retains the first error and
invalidates state even after expiry. Failed safe-off quarantines only the
relevant resource. Session storage must not move or be copied while claimed.
Context, frame, surface, workspace and asynchronous operation generations remain
where they reject stale work. Persistent settings and MicroPython sessions have
separate lifetimes; MP resources retire only after worker terminal handoff.
Camera frame borrows, KPU completion and core1 executor ownership remain explicit.
The Cube audio service gives RECORDER, FILES and MicroPython exclusive use of
one DMA-backed audio path. RECORDER and FILES share the WAV player and all
three consumers use the persisted volume. Audio services own streaming state
and SD file handles. The K210 HAL owns the codec, I²S and DMA3.

Camera register control is separate from image capture. Board `camera_control`
metadata statically selects the private DVP-SCCB or I2C register backend and its
7-bit address/timing. Sensor drivers own register sequences and input format;
camera session passes that neutral format through capture/stream to DVP. DVP
HAL contains no sensor-model selection or register-bus implementation. Register
operations use a shared absolute deadline across both read phases, propagate
errors separately from valid byte values, and require reinitialization after
transport failure. Session-owned probe/reinit retires capture only when no frame
borrow is outstanding, preserving light, SD-poll and frozen-preview policy.

Cooperative deadlines are not forced preemption of arbitrary C callbacks, and
fixed buffers are not memory isolation for untrusted native code. New shared
abstractions should serve a present use case and replace more complexity than
they add. Ordinary changes update code, consumers, tests and relevant API docs;
they do not require another ADR, evidence schema or governance checker.

`firmware/targets/full.c` is a small composition root. It configures the runtime loop in `runtime/hk_main.c`, whose input polling and sleep timing remain platform-dependent. `core` owns app contracts, screen model, dispatch contracts, and neutral data contracts such as `core/pixel_source.h`; it does not access `hk_input` or `hal_time` directly.

## Startup

`runtime/firmware_startup.c` owns startup orchestration. It initializes the platform clocks and hardware through `platforms/k210/startup/platform_bootstrap.c`, loads persisted settings, applies brightness and illumination/RGB settings, initializes the boot controller, shows the boot screen, and mounts storage. The neutral autostart controller then opens the persisted registry target or falls back to the menu; it never includes feature headers.

`platform_bootstrap` is limited to board, HAL, LCD, and hardware-driver initialization. `controllers/boot_controller.c` registers shell callbacks, prepares the menu view, and writes the boot banner; feature view initialization belongs to each app's `start` callback.

## Layer boundaries

- `drivers` contain board-independent device APIs. The selected `boards/<id>` BSP owns board wiring and `platforms/k210/hal` owns K210 SDK access.
- `runtime` adapts core lifecycle and startup to platform facilities.
- `controllers` coordinate scenarios and pass state to services and UI views.
- `services` own runtime operations such as camera sessions, settings application, debug console I/O, LCD screenshot sourcing, and screenshot UART streaming.
- `storage` encodes and persists data. `storage/screenshot_bmp.c` only encodes BMP using a supplied `screenshot_pixel_source_t`; it never accesses the LCD or UART.
- `ui` renders views through the typed Display session; raw LCD transport stays below its provider.

`core/image_viewport.*` calculates a centered source crop and destination area
from explicit dimensions and a `contain` or `cover` mode, without board or
display dependencies. The camera's presentation policy in
`config/camera_layout.h` selects `cover`: preserve proportions and fill the
display. `ui/camera_view` applies board mounting rotation first, then uses the
same viewport for cached pixel sampling, detection boxes and label positions.
Detections outside the visible crop are omitted and intersecting boxes are
clipped. Capture, AI and stored-photo coordinates retain the full sensor frame.
The file viewer uses the existing `ui/image_fit` adapter with `contain`, so a
saved image remains entirely visible.

Screenshot UART output keeps the `HKSHOT BEGIN BMP24` / `HKSHOT END` protocol and CRC. `services/screenshot_source.c` supplies the LCD shadow source, `storage/screenshot_bmp.c` encodes bytes, and `services/debug_screenshot_stream.c` sends them through `services/debug_console_service.c`.

`tools/check_arch.py` guards include boundaries, forbidden compatibility headers, SDK-token placement, include cycles, and feature-module ownership.

The shared AI platform is split across the normal layers. `core/ai_model_types.h`
describes tensors, normalization, post-processing, and exact KModel contracts.
`storage/ai_model_storage.*` owns aligned FAT32 loading plus the optional
CRC-protected SD manifest. `services/ai_model_runtime.*` owns the single-KPU
lease, descriptor/manifest/model-identity/output validation, asynchronous run timing, and
deferred stop/unload state machine. `platforms/k210/hal/hal_kpu.*` remains limited to SDK and
peripheral operations. None of these shared files knows about a camera, DVP
capture policy, a feature app, or a particular post-processor.

Camera KPU consumers share one aligned 320x240 planar RGB input through
`services/camera_ai_input.*`. For unrotated RGB565 sensors, neutral conversion
hooks in `camera_stream` arm DVP AI output at frame start and freeze it at frame
finish. For YUV input or mounting rotation, the main-loop preview adapter uses
its existing frame lease to prepare upright RGB888 with the pure
`core/camera_ai_transform` conversion and inverse box mapping. This handles
K210's luminance-only AI output for YUV without changing sensor or DVP setup.
Both paths give KPU a complete immutable input with its camera sequence,
without reserving another frame-pool slot. `services/core1_executor.*`
similarly owns the one reusable core-1 loop and accepts one bounded job at a
time; feature modules do not replace each other's core-1 entry point.

The shared `settings_menu` controller handles rows, navigation, hold-repeat and
dynamic choices. CAMERA, QR, APRILTAG, OBJECT DETECT and SETTINGS supply their
own descriptors and side effects. SETTINGS uses the App SDK panel and groups
the rows under Screen, Lights, Connection, Startup and Device. Boards with
audio also show Sound. Range and choice edits in SETTINGS use a draft. Lighting
and Screen → UI Color preview edits live. OK saves the value and BACK restores
the previous setting. UI Color selects the accent used by the launcher and
built-in views. Camera frames, saved media and explicit script drawing colors
retain their own colors. Auto Sleep can be disabled and applies while the
launcher is idle. Grove Baud is shown in UART mode.

Device shows the total space of the mounted FAT32 data area and the progress
of the used-space scan. The SD service counts FAT entries in batches while
Device is open and displays the used space once the scan completes. It caches
the count for the mounted card. FAT writes adjust the count for sectors
already scanned.

SETTINGS → Startup can select an eligible built-in app or a pinned MicroPython
script. The script name is stored in userfs and the boot controller opens it
through the launcher shortcut path. Unpinning or deleting the selected script
clears the target. An unavailable target returns to the launcher.

APRILTAG Distance keeps fractional corner coordinates in the unrotated 160×120
full-frame coordinate system and uses a planar pose estimate to recover translation in
millimeters. It stores the known tag side and a per-device focal calibration.
The initial calibration assumes square pixels and a centered principal point.
On Maix Cube, the optional accelerometer can project that translation onto the
horizontal plane after two gravity references are captured. See the
[measurement procedure](../examples/markers/README.md#distance-measurements).

Backlight, illumination, and RGB writes cross the public Lights capability. Its
fixed K210 provider owns exclusive overlapping channel masks, validates
cancellation/deadlines before register writes, and performs safe-off cleanup.
Settings and temporary camera/MicroPython policy remain above that provider;
only the K210 adapter includes the lights driver interface.

Display 0.1 is implemented by the production K210 provider and the deterministic
fixed-capacity host fake. The provider owns BASE/OVERLAY plane sessions, bounded
batch/surface staging, clipped dirty regions, borrowed-buffer lifetime, present
retry, repair, and cleanup. A private UI binding holds the stable typed BASE session;
MicroPython holds OVERLAY. Both reuse the one ST7789 shadow framebuffer over a
raw transport that has no app, run-ID, or Python policy. The provider reports
composition-specific bounded batch limits: the full profile reserves the
retained MicroPython canvas, while a MicroPython-disabled composition keeps a
minimal truthful batch implementation and does not reserve that absent
consumer's command/text budget.

## Feature modules

Menu applications are self-contained modules: `apps/terminal/`,
`apps/camera/`, `apps/qr_camera/`, `apps/face_detect/`, `apps/face_recognition/`, `apps/apriltag/`,
`apps/object_detect/`, `apps/micropython/`, `apps/files/`, `apps/buttons/`,
`apps/pong/`, `apps/settings/`, POWER in `apps/sleep/`, and Cube's
`apps/recorder/` and `apps/accelerometer/`. Each owns its app entry point,
controller, view, icon, feature configuration, and feature-specific
state/services. The only public header of a module is its `*_app.h`, and the app
implementation owns its typed lifecycle entry. Generated registry code uses typed
extern entry objects and does not include app-private headers.

The build manifest maps each app ID to its whole directory. `--disable-app`
therefore removes every source and private header of that feature. Shared camera
sources remain while CAMERA, QR-CAMERA, FACE DETECT, FACE RECOG, APRILTAG, or OBJECT DETECT
is enabled; `quirc` is staged only for QR-CAMERA. The planar AI input is staged
only for FACE DETECT, FACE RECOG and OBJECT DETECT. The shared core-1 executor is retained while APRILTAG,
MICROPYTHON, or QR-CAMERA needs it. With no camera consumer, sensor/DVP/camera runtime sources
are omitted while the general KPU HAL remains available.

Canonical manifests generate the immutable descriptor and menu arrays. The
small app-neutral core registry dispatches primary and secondary screen
ownership, menu icons and explicit debug commands. The single foreground
runtime dispatches lifecycle callbacks, timer and SD events; inactive apps do
not receive background ticks. Shared screen, SD, debug, boot, and system-tick controllers
do not include feature headers or select features with conditionals.

CAMERA owns photo capture orchestration, the file writer and paths, settings,
and its view. The shared photo encoder also serves MicroPython's BMP output.
QR-CAMERA owns its scan scheduling, result view, text persistence and settings.
It uses the same quirc wrapper and luma converter as MicroPython. The
converter restores the order of RGB565 pixel pairs from the K210 DVP
buffer. Quirc fits the grid using the K210's single-precision FPU and
tries nearby sample positions if the center samples fail to decode.
Both native apps reuse the shared camera session, sensor and frame
pipeline, preview renderer, and settings persistence.

A stalled DVP conversion is rearmed in place. Ready frames and outstanding
leases remain valid across recovery.

Camera previews leave a minimum gap after a successful update on the parallel
LCD. The gap counts time already spent elsewhere in the main loop.

The QR preview consumer fills the decoder's uncached luma image from the current
frame. Core 1 owns quirc and detects codes while capture and display continue.
The main loop takes results after the worker ticket completes and pauses the
preview when it opens the result window. Exit discards pending results and
defers decoder cleanup until the worker releases the image.

FILES owns browser navigation/state, previews and deletion, all BMP/PNG/PPM/RAW/GIF decoders, and its view. Shared FAT32 provides neutral mount, directory scan, file, allocation, and stream contracts and has no dependency on FILES browser state. BUTTONS, system SETTINGS, and POWER likewise own their lifecycle and presentation.

POWER replaces the separate SLEEP and POWER OFF menu entries. Its manifest ID is
`power`; the existing `apps/sleep/` directory retains ownership of the complete
module. LEFT/RIGHT selects SLEEP, POWER OFF or RESTART; one fresh OK executes the
selected action, without an additional confirmation. An OK held while opening
POWER cannot trigger an action until released and pressed again. BACK closes the
selector. Automatic sleep opens POWER directly in sleep mode, and a new button
press wakes it. The shared auto-sleep controller observes inactivity without
polling inactive apps. The selector draws through the public borrowed Display
surface, avoiding per-pixel queued commands and their fixed-capacity limit.
Its power glyph is shared with the main menu as a neutral bitmap asset.

FACE DETECT owns its view, icon, and debug command. FACE DETECT and FACE RECOG
share the detector's model descriptor, YOLO decoding, planar input and teardown.
They load `/hackylens.kmodels/detect.kmodel` from microSD. [FACE RECOG](FACE_RECOGNITION.md)
keeps two companion models resident under the same KPU lease, runs one
inference at a time and publishes matching IDs through the shared vision API.

APRILTAG owns its settings and selection model, settings menu adapter, result
overlay, icon, and `HKTAG`/`HKTAGINFO` commands. It shares the TAG36H11 decoder
with MicroPython and uses the shared 320×240 camera runtime. Core 0 fills a
single uncached 160×120 luma buffer when the core-1 executor is available.
Frames that arrive while the decoder is busy are skipped. Core-1 jobs publish
completed result banks. The app processes each completion once and presents
the LCD preview on a separate cadence of up to approximately 12 updates per
second. The on-screen FPS counter reports LCD updates.

NEAR downsamples the whole frame. FAR uses a native 160×120 window and
maps corners back to the same full-frame calibration coordinates. The window
starts centered, follows detections and shrinks to 64×64 for small tags during
full-detail tracking. The app periodically searches the full frame
after misses. FAR can retry the window without decimation. Both modes compose
the corner contour into the rotated LCD preview and publish native tag IDs
and capture-space bounding boxes through `vision_result_service`. The
`ALL/SELECTED` filter applies to the shared BLOCK snapshot. Refine Edges,
the FPS display and the illumination profile are app preferences. The
BSD-licensed OpenMV AprilTag core is staged from `firmware/third_party/apriltag`
when the feature is enabled.

OBJECT DETECT owns its VOC20 labels, settings adapter, overlay, icon, and
`HKOBJECT`/`HKOBJECTINFO` commands. It shares the YOLOv2 decoder and NMS
with MicroPython. Its pinned KModel v3
accepts planar `1x3x240x320` bytes and returns `1x125x7x10` floats. KPU
inference is asynchronous; completed output is decoded on core 0 before a new
DVP input is armed, avoiding K210's non-coherent cross-core cache boundary.
Newer camera frames are discarded rather than queued. Double result banks and
session epochs prevent partial or
pre-settings results from becoming visible. Boxes are composed before the
single LCD present and published as the existing transport-neutral BLOCK
format with class IDs `0..19`.

Terminal owns its bounded line ring, viewport, scrolling, font geometry, and log-sink lifecycle. The shared logging service exposes only a generic optional sink and remains independent of Terminal. Font selection remains in reserved feature bits inherited from the legacy settings payload, so old records and erased flash continue to decode as `TERMINAL_FONT_NORMAL`.

Settings record v5 retains the v4 data layout and widens the stable autostart
ID from uint8 to uint16 without increasing the aligned 124-byte record. The 88
opaque app bytes remain unchanged: bytes `0..79` contain APRILTAG preferences
and its 587-bit selected-ID map; OBJECT DETECT owns bytes `80..87`. The storage
layer validates and migrates v1, v2, v3, and v4 records. V4 autostart values are
zero-extended, so existing IDs `0..10` and all CAMERA, external-link, APRILTAG,
and OBJECT data remain unchanged.

## Architecture guard

`tools/check_arch.py` uses one declarative table for the feature directories. It rejects legacy paths, flat app implementations, external inclusion of private feature headers, private settings-menu view access, layer inversions, include cycles, a mismatch between feature directories and the build manifest, app access to AI storage/HAL internals, and camera/feature dependencies in the shared AI platform.

## Supported hardware and current qualification

HackyLens ships full builds for HUSKYLENS SEN0305 and Sipeed Maix Cube.
Both include FACE RECOG and embedded MicroPython. SEN0305 has thirteen apps.
Cube has fifteen and also supports WAV playback in FILES. Its additional apps are RECORDER
and ACCELEROMETER. The latter uses the same read-only typed binding as
MicroPython. The board provider owns the internal I²C1 transactions. See the
[v0.8.0 release notes](releases/v0.8.0.md) for the current hardware coverage.
Each build explicitly selects a board. Its `board.toml`
owns wiring, defaults, flash layout and programming metadata. On Cube,
settings and userfs writes require JEDEC to report exactly 16 MiB of flash.

During v0.5.0 validation, both boards passed cold startup and camera checks
covering all three capture sizes, colorbar, live probe/reinit and switching
among CAMERA, QR-CAMERA and APRILTAG. On Cube, a user confirmed a visible
FACE detection box. OBJECT loaded the VOC20 model and completed inference
after QR-CAMERA. The final Cube build passed HMPY upload, read, run, STOP
and cleanup while FACE was active. The user confirmed POW/BACK navigation
after PMIC bus recovery. See the [v0.5.0 release notes](releases/v0.5.0.md)
and the [Cube hardware log](MAIX_CUBE.md) for the tested builds and scenarios.

The Cube hardware log records [optical AprilTag recognition](MAIX_CUBE.md#micropython-qr-and-apriltag-checks-2026-11-12)
and [QR timing on an on-screen card](MAIX_CUBE.md#qr-camera-pixel-order-2026-11-13).
MicroPython read 20 of 20 presentations within one second. QR-CAMERA
read 19 of 20 within one second with a reboot before each trial. A
separate check on the build with the QR worker cache fix confirmed that
scanning resumed on all 20 app opens without a reboot. VOC20 accuracy
remains to be measured on Cube.
The maintainer waived the physical exchange test for Cube's
Grove UART/I2C. The [hardware log](MAIX_CUBE.md#browser-color-tracker-checks-2026-11-12)
records IDE threshold selection, physical launches, BACK and microSD output
on both boards. MicroPython has separate acceptance checks for 1000 run/stop
cycles, flash endurance and power loss, and [WDT1 recovery](WDT_HARDWARE_ACCEPTANCE.md).

### Earlier SEN0305 qualification

Firmware `8527aab` passed hardware checks on SEN0305 (COM10). Its full raw
image was 1,545,272 B and static RAM (`data + bss`) was 2,893,216 B.
The MicroPython-disabled raw image is
1,353,528 B. Full raw SHA-256 is
`811be8cdc6ea18b66fa1d657e1c3850de568ae1e58c76a8408b1306e22bbf18b`.
Against accepted S7 `0405a09`, this saves 21,120 B flash and 5,256 B static RAM.
The 227-test suite, both builds, architecture/provider evidence and resource
guard passed; CI for documentation HEAD `1f304e4` passed in run `35564255150`.

The consolidated acceptance includes user-confirmed QR, Input, Lights, Display,
Sleep and FILES/GIF; the user confirmed remaining UART/I2C exchange on
2026-09-21. Automated device checks cover boot, settings/camera/Pong/menu
switches, four MP terminal paths and an external MP OVERLAY run surviving native
SETTINGS → MENU switches. CAMERA present was observed near 31 ms; this is a
smoke observation, not a matched-workload latency distribution. No repeat of
unaffected accepted checks is needed for documentation/tooling cleanup.

Heavy GIFs can still play below nominal speed, and button response may wait for
a frame to finish. See the [roadmap](ROADMAP.md) for remaining product and
research work.

A useful Python-to-native experiment separates domain state from hardware,
implements equivalent behavior through these shared services, then compares
correctness, code changes, latency and memory under the same fixtures. No
skeleton generator or project manager is a prerequisite.

### Simplification resource comparison

The starting revision `71ec63fffe2926642fba00ff8d28519fd93750d9` was rebuilt
with the pinned K210 toolchain for this comparison. Full raw image: 1,562,168 B;
static RAM: 2,860,328 B. The accepted final firmware is 16,896 B smaller in flash
and uses 32,888 B more static RAM. This is not an across-the-board RAM reduction.
Symbol comparison attributes the main additions to 12 × 1,024 B bounded app
state and 25,744 B of QR decoder scratch moved from stack to static storage;
removed broker/runtime tables offset part of that growth. The QR scratch change
addresses the working decoder path and stack pressure; it is not governance
metadata or a new full framebuffer. Static RAM alone does not measure peak
stack/heap usage. Older plans and exact resource records can be retrieved from
Git history. `tools/check_resources.py` reports deltas against a historical
SEN0305 build and checks for new direct allocation and task-creation sites.
The firmware build checks the board's firmware partition and verifies that
the linked RAM layout leaves space for the heap after both core stacks.

### Current validation workflow

The host suite covers architecture boundaries, board/service/app composition,
capability contracts, lifecycle cleanup, image metadata and the production K210
rendering path. Run `python -m pytest tests -q` for the current test count and
results, and `python tools/check_arch.py` for the layer/dependency guard.
CI builds the full firmware for both boards and checks each app composition.
It also checks reduced SEN0305 builds. On a release tag, CI validates each
full image against its board-specific sidecar and attestation, then prepares
each firmware and sidecar as individual downloads alongside one shared SD-card ZIP with
`tools/package_release_bundles.py`.
