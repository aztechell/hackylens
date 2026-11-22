# MicroPython in HackyLens

This report records the research and implementation plan started on 2026-08-04.
The hardware acceptance scope was updated on 2026-11-20 and the results on
2026-11-21. See [the implementation and validation results](#implementation-and-validation-results)
for the builds and devices actually tested.

## Summary

Roadmap item 5 was feasible on the K210 within the existing HackyLens
architecture. The recommended starting point was the official MicroPython
1.28.0 `embed` port. The VM should run as a long-lived task on core 1. The main
loop, display, buttons, USB-to-UART transport, and peripheral ownership should
remain on core 0. Python hardware bindings should access HackyLens services
through a bounded RPC mailbox. SDK drivers stay behind those services.

Scripts should be stored in littlefs on a dedicated partition of the internal
NOR flash. The SEN0305 examined for this report had 16 MB of flash. The original
firmware package uses addresses through `0x00C6E000`. The preliminary user
partition was therefore `0x00C70000..0x00FFFFFF` (3,735,552 bytes, or 912 sectors
of 4 KB). The firmware should enable the partition only after verifying the
JEDEC capacity. On 8 MB variants it should report `UNSUPPORTED_FLASH` and
disable writes to the proposed user partition.

The first release was planned in three steps. First came reliable storage and
the protocol, followed by a minimal VM with safe stopping, then bindings and
the IDE. Camera, KPU, and SD bindings were deferred because of more complex
resource ownership, large buffers, and asynchronous lifecycle requirements.

## Baseline before roadmap item 5

- Before MicroPython was added, the full firmware image was 1,312,632 bytes.
  The ELF contained 2,415,168 bytes of `.bss` and its static data ended near
  `0x8038E240`. These figures were used for initial sizing. The measured build
  sizes appear in the implementation results below.
- The firmware already had feature modules, `hk_app_t`, safe app entry and
  exit, a shared `core1_executor`, a UART3 debug console, and a ring-buffer
  terminal.
- The original `boot_flash` supported JEDEC ID, read, sector erase, and page
  program. It still needed range and page checks, timeout status, mutual
  exclusion, and error propagation for filesystem use.
- Settings v4 occupies two sectors at `0x007FE000` and `0x007FF000`. The build
  guard keeps the image from crossing into the first slot. The settings layout
  should be preserved.
- The PC connects to UART3 through a USB-to-serial bridge. The protocol proposed
  in the roadmap can run over that existing serial link.
- The original firmware package confirms the 16 MB layout. The main image
  starts at zero, model data extends to the end of `object_detect.bin` near
  `0x00C5BDE8`, and service-sector erasure ends at `0x00C6E000`.

Local sources include `tools/build_firmware.py`,
`firmware/src/storage/settings_store.c`, `firmware/src/drivers/boot_flash.c`,
`firmware/src/services/core1_executor.c`, `build/sdk-full/hackylens_full`, and
`../unpacked/flash-list.json`.

## Choosing a MicroPython port

### Upstream `embed`

The official MicroPython Tier 2 `embed` port generates a self-contained set of
`.c/.h` files for use in an existing C project. Its example takes a GC heap from
the host, executes source strings, and calls deinit. This lets HackyLens remain
the host firmware and create the Python VM only for the duration of a run.

The lockfile should pin these inputs.

- MicroPython `v1.28.0` and its full commit SHA
- The `mpconfigport.h` configuration
- The generated source list or a reproducible `embed.mk` invocation
- The `.mpy` version. In v1 the IDE should send `.py` source to avoid a
  dependency on the bytecode ABI.

The compile and link audit reduced the initial estimates of a 256–512 KiB
heap and a 32 KiB Python stack. The resulting v1 VM used the following
configuration.

- `MICROPY_CONFIG_ROM_LEVEL_MINIMUM`, compiler, GC, exceptions, `sys`, `gc`,
  and explicitly enabled `min/max` builtins
- Single-precision floating point
- REPL, `_thread`, network, SSL/TLS, native/Viper emitters, dynamic native
  modules, persistent-code loading, VFS imports, and arbitrary memory access
  disabled
- The `hackylens` module with controlled objects in place of the standard
  `machine` module
- A static, aligned 128 KiB GC heap owned exclusively by core 1
- A 24 KiB Python stack limit with a 1 KiB stack-check margin and a source
  file limit of 65,535 bytes
- Heap, source, and output buffers reserved once and reused.
  `mp_embed_deinit` clears VM state without fragmenting the C heap between runs.

V1 supports one source entry point and built-in modules. Imports of additional
user `.py` files from littlefs should be added separately through a bounded
reader or import hook. That interface must keep the rest of the flash block
device and the SD card outside the VM's reach.

The official minimal embedding example runs with an 8 KB heap to demonstrate
the API. Size the heap for the application workload. MicroPython has a
separate Python heap, so measure the Python and C heaps independently.

### MaixPy-v1 as a K210 reference

MaixPy-v1 demonstrates that MicroPython, the KPU, camera, LCD, and other
peripherals run on the K210. It is a complete platform based on an older
MicroPython/OpenMV fork and its own build system. The repository describes it
as K210 MicroPython firmware and no longer maintains the historical branch.
Porting it in full would introduce another HAL, a second owner for the
camera, LCD, and UART, and a separate lifecycle.

MaixPy is a reference for K210-specific RISC-V builds, GC and stack collection,
cache barriers, and binding implementations. Copy only the necessary parts
after checking each file's license. MaixPy-v1 uses MIT/Apache-2.0, with the
MicroPython/OpenMV portions marked MIT.

## Execution and safe stopping

### Core assignment

A synchronous `mp_embed_exec_*()` call from `app.tick` on core 0 would let an
infinite `while True` block the buttons, display, debug protocol, and system
tick. Run the VM as one job on the existing `core1_executor`.

The startup sequence proceeds as follows.

1. Core 0 closes the previous application and checks that core 1 is free.
2. Storage reads the selected `.py` into a bounded buffer and checks its size.
3. The runtime attaches the statically reserved GC heap, creates the resource
   ledger, and publishes the run epoch.
4. Core 1 captures its own stack top, initializes the VM, and executes the
   source.
5. Core 0 continues the UI tick, services binding calls in the mailbox, and
   drains stdout.
6. After the script returns or raises an exception, core 1 deinitializes the VM
   and publishes the result. Core 0 releases all leases before allowing a
   return to the menu.

MicroPython and APRILTAG share one executor and run exclusively. Wait for
APRILTAG's deferred destroy to complete before starting Python. The executor
could later gain an owner ID and a cancellation contract while retaining a
single entry point for core 1.

### Cooperative stop and watchdog recovery

Cooperative stopping is the normal path.

- `MICROPY_VM_HOOK_LOOP`, `MICROPY_VM_HOOK_RETURN`, and both central
  `mp_iternext*` entry points check the atomic `stop_requested` flag and
  deadline. The pinned patch also covers native C loops in `sum`, `min/max`,
  and constructors.
- The hook raises `KeyboardInterrupt` in the VM.
- `time.sleep*` is split into intervals of at most 10 ms and checks for stop
  between them.
- Each C binding needs a finite timeout and cancellation points.
- Native/Viper code is disabled to keep long-running Python code subject to the
  bytecode VM hooks.

The time limit should come from the launch policy. The IDE can request a limit
up to the firmware maximum. In v1, `0` selects the 30-second default and the
maximum is 300 seconds. STOP is available at any limit.

The hardware watchdog is the emergency recovery path. If core 1 remains
active two seconds after STOP or five seconds after the deadline, its C code
may still own a resource. Returning to the menu requires a hardware reset.
Core 0 arms WDT1 once and waits for reset. Normal flash operations do not run
under this watchdog. The firmware records the WDT1 cause and suppresses
firmware autostart for that boot to avoid a boot loop. Entering the native
app never runs the startup script by itself. An explicit manual RUN is still
available.

### Resource ledger

V1 uses a small fixed dependency order. It takes the display lease while
preparing the run and records external UART/I2C and LED/RGB state lazily on
first use. Idempotent cleanup stops the external transaction, resumes the
connector service, restores the light settings, and removes the display
overlay last. Executable tests verify that each cleanup operation runs exactly
once in reverse order. These three independent groups need no shared order
stack. Python receives no raw pointers, FPIOA numbers, SPI flash or watchdog
access, or clock controls. Errors, exceptions, STOP, and app exit use the same
cleanup state machine.

## Flash storage

### Proposed layout

| Range | Size | Purpose |
| --- | ---: | --- |
| `0x000000..image_end` | Variable | HackyLens firmware |
| `0x007FE000..0x007FFFFF` | 8 KB | Existing settings slots |
| `0x00C70000..0x00FFFFFF` | 3,735,552 bytes | littlefs scripts |

The ranges between the image, settings, and `0x00C70000` remain reserved
because the original firmware used them for models and the splash screen.
The firmware image can stay smaller than the partition.

Release metadata needs a versioned flash layout descriptor. The flasher
updates only the firmware by default and preserves userfs. Separate explicit
`format-userfs` or `erase-userfs` commands may erase it. A full restore of the
original package may erase the Python files.

The firmware checks the flash and filesystem before mounting.

1. Read the JEDEC ID.
2. Derive capacity from the density byte and validate it against the allowlist
   of tested chips.
3. Verify that partitions are aligned and do not overlap.
4. Disable userfs on capacities below 16 MB.
5. Require user confirmation or an IDE command before formatting a damaged
   filesystem.

### Why littlefs

Littlefs is designed for NOR flash and supports copy-on-write, power-loss
resilience, dynamic wear leveling, bounded RAM usage, and atomic rename and
remove operations. These features make it a better fit than a custom
append-only journal once the system needs list, read, delete, and multiple
files. Its BSD-3-Clause license is compatible with the project's MIT license.

The initial block configuration needs validation on the physical chip.

- `read_size = 1` or the confirmed minimum driver granularity
- `prog_size = 256`
- `block_size = 4096`
- `block_count = 912`
- `cache_size = 256`, with two static cache buffers
- `lookahead_size = 32` initially, then tune based on measurements
- `block_cycles = 500` as a starting point for littlefs wear leveling

Littlefs uses CRC to protect metadata. The transport also checks the CRC32 of
the complete upload before atomic commit. The upload goes to a temporary
`.upload-<id>` file. After `close/sync`, the file is read back, verified, and
renamed atomically. Incomplete temporary files are removed on the next mount.

Littlefs depends on the following changes to `boot_flash`.

- Range checks and page-boundary splitting
- `OK/TIMEOUT/IO/BOUNDS` results in place of `void`
- Read-after-program where transport verification requires it
- A shared lock or owner for settings and littlefs
- Erase and program restricted to core 0
- Block-device tests with a RAM or file backend and fault injection after
  each program or erase step

## Serial protocol v1

Arbitrary stdout must be kept separate from the existing line-based `HK...`
commands. The following handshake preserves compatibility with line mode.

1. In line mode, the IDE sends `HKMPROTO 1`.
2. The device answers `HKMPROTO 1 READY` and switches UART3 to exclusive
   framed mode.
3. Subsequent responses, stdout, and events use frames.
4. `SESSION_CLOSE`, a disconnect timeout, or reboot restores line mode.

A frame consists of the COBS payload and a `0x00` delimiter. The table shows
the decoded layout.

| Field | Bytes |
| --- | ---: |
| Magic `HMPY` | 4 |
| Version | 1 |
| Type | 1 |
| Flags | 2 |
| `request_id` | 4 |
| `payload_len` | 4 |
| Payload | N, up to 1024 in v1 |
| CRC32 of header + payload | 4 |

COBS lets the receiver find the next frame boundary at the zero delimiter.
CRC32 detects corruption. The IDE uses `request_id` to match responses.
Asynchronous events use zero. All integers are little-endian. The parser has
a fixed buffer, drops oversized frames, and resynchronizes at the next
delimiter.

The minimum message set includes these requests, responses, and events.

- `HELLO` returns protocol, firmware, board, flash profile, capabilities,
  and limits
- `LIST`, `STAT`, `READ`
- `UPLOAD_BEGIN`, `UPLOAD_CHUNK`, `UPLOAD_COMMIT`, `UPLOAD_ABORT`
- `DELETE`, `SET_STARTUP`, and `FORMAT` with a separate confirmation token
- `RUN`, `STOP`, `STATUS`
- Events `STDOUT`, `STDERR`, `STATE`, `FILE_CHANGED`, `DROPPED`
- `SESSION_CLOSE` and `PING`

Every upload includes the expected size and CRC32. Chunks carry offsets.
Delivering the same chunk again is idempotent. Gaps and overlaps return an
error. Stdout uses a bounded 4 KiB ring buffer. On overflow, it reports the
discarded bytes with `DROPPED(bytes)`.

## Bindings v1

The original public v1 API deliberately used a small, flat interface.

```python
import hackylens as hl

if hl.button(hl.BUTTON_BACK):
    print("back")

hl.display_clear(0x0000)
hl.display_text(8, 8, "Hello", 0xffff)
hl.display_present()
hl.led(50)                  # 0..100
hl.rgb(255, 0, 102)         # 0..255 per channel
hl.sleep_ms(100)
```

Namespaced objects, button edge events, and normalized RGB values from
`0..100` belonged to an earlier design. The canonical contract is in
[`MICROPYTHON_API.md`](MICROPYTHON_API.md).

The original v1 interface had these limits.

- `buttons` exposes level snapshots and reads. Edge state and Python IRQ
  callbacks are unavailable.
- `display` uses a bounded command buffer, clipping, and one Python overlay.
  Core 0 updates the LCD shadow buffer and presents it.
- `time` provides wrap-safe ticks and sleep.
- LED accepts `0..100` and RGB accepts `0..255` for each of three channels.
- UART/I2C uses only the external connector through the existing HAL and
  services, with an exclusive lease, a buffer limit, and a timeout. The
  regular external-link service pauses during the lease. Python cannot
  access the UART3 debug port.
- STOP/BACK takes priority over button reads by the script.

The next bindings for camera, KPU, SD/files, and vision results were planned
after the runtime stabilized. They should reuse `camera_session`,
`ai_model_runtime`, `file_*`, and `vision_result_service`. Those services keep
MaixPy APIs and raw SDK calls outside the Python interface. The KPU retains
its single loaded model rule.

## On-device application

The `firmware/src/apps/micropython/` feature folder should have one public
`micropython_app.h` header and be removable as a unit using
`--disable-app micropython`. Internally it needs a controller, view, runtime
adapter, file model, and configuration. The littlefs block device stays in the
shared storage subsystem because the IDE protocol also uses it. The build
manifest should omit the VM, littlefs, and the protocol when the MicroPython
app is disabled.

The proposed UI covers the basic file and execution workflows.

- A list of `.py` files, with a startup indicator and available space
- RUN/STOP, DELETE with confirmation, and SET/CLEAR STARTUP
- A read-only source viewer that reads one page at a time
- A log screen with follow, scroll, and final status
- BACK during a run first requests a stop and stays on the run screen
- A return to the menu only after cleanup
- Holding BACK during boot suppresses MicroPython firmware autostart
- The app shows the previous failure reason after a watchdog reset

The initial plan kept startup-script execution out of the common boot
orchestration. The firmware saves the app autostart ID. Starting the app at
boot or from the menu opens the same script list. Startup metadata selects
the default file, which is highlighted first and is the target of HMPY
`RUN` without a filename.

## IDE based on Pybricks Code

Pybricks Code is an MIT-licensed TypeScript/React web IDE with Monaco,
project storage, and a terminal. Its UI was a suitable base. Its transport
and device model target LEGO Bluetooth/USB and Pybricks firmware. The plan
was to keep a separate fork pinned to an upstream commit, with the
`LICENSE`, copyright, and a visible attribution page.

The plan retained these components.

- Monaco, tabs, project persistence, and file import and export
- The terminal
- The installable PWA and its offline shell
- State management where it is independent of LEGO hubs

These components needed replacement or removal.

- BLE hub transport, LEGO discovery, and the firmware installer
- Pybricks-specific mpy-cross and firmware metadata
- LEGO API docs and analysis, replaced with `hackylens` module stubs and docs
- The run and download flow, replaced with one HackyLens Web Serial client

Web Serial requires a secure context and has limited browser support. The
web version targets desktop Chromium and Edge on HTTPS or localhost. A
desktop wrapper could serve Firefox and Safari users and fully offline
Windows installations. It could reuse the same protocol client without
requiring a firmware protocol change.

The IDE MVP covers connection, HELLO, file listing and management, run and
stop, live stdout, startup selection, and reconnection. Formatting,
autocomplete, and simulation were left for later.

## Licensing

- HackyLens uses MIT.
- MicroPython uses MIT. Keep its copyright and permission notice.
- Pybricks Code uses MIT. Keep the Pybricks Authors copyright and add
  visible attribution.
- Littlefs uses BSD-3-Clause. Include its notice with source and binary
  distributions.
- MaixPy-v1 includes MIT and Apache-2.0 code. Check each borrowed file and
  record it in `NOTICE`.

The release plan called for a generated third-party manifest containing the
version, commit SHA, license, and list of included files.

## Implementation milestones

### Stage 0 — Hardware proof

1. Read the JEDEC ID and confirm 16 MB on the available SEN0305.
2. Test erase, program, and read at `0x00C70000` while preserving settings,
   firmware, and the ability to restore the original package.
3. Add the flash layout descriptor and bounds checks to the build, release,
   and flasher.

Acceptance requires confirming the JEDEC ID, the layout, and storage
operation on the available SEN0305, with writes confined to the user
partition.

On 2026-11-20 the project owner excluded physical NOR endurance testing,
deliberate power cuts during writes, and testing of additional SEN0305 units
from hardware acceptance.

### Stage 1 — Storage and protocol

1. Harden `boot_flash`, add the littlefs block device, and add host fault tests.
2. Implement atomic upload, list, read, delete, and startup metadata.
3. Implement the framed protocol and reference Python CLI before the IDE.

Disconnecting at any upload chunk must leave the old file or no file, with
no partial file published. The line-based debug interface must recover.

### Stage 2 — Minimal VM

1. Integrate the pinned upstream embed port without bindings.
2. Execute source on core 1 and deliver stdout and state on core 0.
3. Add heap, stack, and time limits, hooks, STOP, deinit, and watchdog recovery.

Normal return, syntax and runtime errors, OOM, recursion, infinite loops,
sleep, and stdout floods must end predictably. The 1,000-cycle RUN/STOP test
must preserve the C heap footprint and menu operation.

### Stage 3 — V1 bindings and on-device app

1. Add buttons and time, then LED/RGB, display, and UART/I2C.
2. Add lease, timeout, cancellation, and cleanup tests for each binding.
3. Add the browser, log and startup UI, and safe boot.

STOP during each blocking API call must return control or offer a controlled
restart. All resources must be restored after stopping.

### Stage 4 — IDE

1. Pin the Pybricks Code fork and add attribution.
2. Add the transport client and the initial file, run, and log UI.
3. Verify reconnection, duplicate chunks, CRC errors, and firmware mismatch.

The full workflow must support editing, atomic upload, running, live logs,
stopping, and running from the device without an SD card or manual debug
commands.

### Stage 5 — Camera and KPU

Extend the bindings after measuring v1 latency, RAM, and cleanup. Start with
read-only vision results, then add camera snapshots, then models. Preserve
the existing single-owner contracts at each step.

## Implementation and validation results

The prototypes confirmed that the K210 can use upstream MicroPython
directly. The official MicroPython v1.28.0 embed port at commit
`e0e9fbb17ed6fd06bb76e266ae554784c9c80804` generated 135 C files. All compiled
with GCC 8.2 using the firmware's `rv64imafc/lp64f` flags and linked with
HackyLens. The minimal VM added 135,038 bytes of text, 8 bytes of data, and
776 bytes of BSS, excluding the GC heap. Upstream already provides RV64 NLR
(`py/nlrrv64.c`) and GC register saving.

The code audit led to these design adjustments.

- The linker script allocates a 32 KiB stack for core 1. The VM uses a
  conservative 24 KiB limit measured from a local stack anchor.
- Core 1 exclusively owns the static, aligned 128 KiB GC heap.
- The mailbox, runtime control, source and output buffers, and binding RPC
  use the uncached alias. K210 also needs this placement when using
  `volatile` and fences.
- Flash and peripherals remain exclusive to core 0. Python bindings use a
  bounded RPC with one call in flight. STOP raises a cooperative exception
  through VM hooks and cancellation points.
- The binding RPC keeps its single-slot context until the final cancellation
  acknowledgement from core 0. This prevents an NLR unwind from leaving a
  hardware operation pending. UART TX uses DMA to feed the FIFO and waits
  for the last stop bit before completing. I2C has a 250 ms service deadline
  and cancellation points.
- The GC hook only updates the heartbeat. An exception inside the collector
  could leave it in an intermediate state.
- Native iterator entry points poll the same stop and deadline hook. An
  overdue cooperative STOP has a one-shot WDT1 fallback that suppresses
  autostart on the recovery boot.
- HackyLens Code lives in the separate `hackylens-code` repository with a
  complete buildable source tree pinned to a verified Pybricks Code commit.
  It includes the HackyLens transport, UI, and tests. The upstream MIT
  license, attribution, and pin are preserved. Large LEGO firmware and
  video assets are excluded.

The implementation includes the canonical flash map, a guarded NOR driver,
littlefs v2.11.2, explicit formatting, atomic upload using
`temporary + sync + CRC32 + rename`, startup metadata, cleanup of incomplete
uploads, the core 1 MicroPython runtime, v1 bindings, the on-device app,
HMPY v1, the Python CLI, and the separate HackyLens Code repository with
Monaco and Web Serial. The IDE has its own CI, build, and release process
outside the firmware and SD card release archives.

The original implementation went through the following software checks.

- The fault harness passes 53 possible interruption points in the mutation
  sequence, plus CRC rejection, abort, delete/startup, and stale-upload
  scenarios.
- All 57 Python host tests pass without skips. They cover Python/C golden
  vectors, corruption, fragmentation, bounds and resynchronization, the
  53 fault cuts, the full fake-device storage/runtime workflow, the safe
  hardware acceptance runner, and an executable harness for the real
  binding/HAL/LCD state machine.
- Thirteen TypeScript codec and client tests across three suites use the
  same golden vectors and cover the full fake-device workflow, long
  operations, and keepalive. Strict TypeScript checking and the production
  webpack build pass.
- `check_arch.py`, `gen_flash_layout.py --check`, and `check_env.py` pass for
  the 12-module tree, the 16 MiB flash map, and the pinned host and target
  toolchains used in this validation.
- The full firmware has text/data/BSS sizes of `1346182/158152/2697472`.
  The binary is 1,504,440 bytes with SHA-256
  `5F265CB4DDF14E1B2C3E852725973782EBD91D39C415CB422943A48B469D7FBD`.
- The symbol check verifies MicroPython, runtime, HMPY, and userfs symbols,
  explicitly requiring `mp_builtin_min_obj`, `mp_builtin_max_obj`, and
  `mp_builtin_sum_obj`. The production release binary is byte-for-byte
  identical to the tested full build.
- The release and HELLO version in the ELF matches the canonical
  `VERSION=0.2.0`.
- The `--disable-app micropython` build contains no MicroPython, HMPY, or
  userfs symbols. It has text/data/BSS sizes of `1157879/158112/2418448`.
  The binary is 1,316,088 bytes with SHA-256
  `3ED2FA8E9F2D357F85EF4BFA5B491645824DEC6C9106493A064EF6C2E7BC11D9`.
- A browser check of the production IDE at 1280×720 confirmed Monaco, all
  disabled and initial states, local actions, and no console errors.

The production image with the same SHA-256 was installed on the physical
SEN0305. The flasher wrote it without a full erase and preserved settings,
the legacy area, and userfs. The boot log confirmed JEDEC `EF6018` (16 MiB),
preserved settings, 12 applications, and a working OV2640. HMPY `HELLO`
returned firmware `0.2.0`, board `HackyLens K210`, and capabilities `511`.
With the camera running, 100 consecutive `PING` requests at the maximum
1024-byte payload succeeded. The ISP stub lacks a flash-read command,
so flasher `--verify` ended with a warning. The device checks confirmed
the version and transport. The exact binary hash was unavailable from
the device because it could not be read back.

The first mount found a damaged userfs with `filesystem_state=3` (`CORRUPT`)
and `filesystem_error=5` (`CORRUPT`). The firmware correctly required
explicit approval to format it. After the exact destructive confirmation
token `ERASE USERFS` was accepted, the partition formatted and mounted
successfully. The full hardware workflow passed on production firmware.
It covered atomic upload/read/LIST/STAT, startup selection and execution,
continuous logs, cooperative STOP (123 ms), STOP inside native `sum` (121 ms)
and `min` (121 ms), executor reuse, transport loss without `SESSION_CLOSE`,
reconnection after the lease, and STOP on a new run (122 ms). Cleanup
removed just the five acceptance files, cleared startup, and left userfs
mounted and empty.

The machine-readable traces are in the gitignored
`build/hardware-acceptance` directory. `com10-format-full-workflow.json`
records a successful `format` step followed by a failure in the older host-side
STDERR check. `com10-postformat-readonly.json` independently confirms that
the empty partition mounted. The final `com10-full-workflow-final.json` and
`com10-final-clean-readonly.json` both report `PASS`.

The main CLI/HMPY hardware workflow passed. The later
[Cube test log](MAIX_CUBE_TEST_LOG.md#browser-color-tracker-checks-2026-11-12) records
browser upload, run, and stop, as well as the physical BACK button on both
boards. [WDT1 reset cause and autostart suppression](WDT_HARDWARE_ACCEPTANCE.md)
passed on SEN0305 and Maix Cube on 2026-11-20.

On 2026-11-20 the project owner excluded physical NOR endurance testing,
deliberate power cuts during writes, and JEDEC/layout checks on additional
SEN0305 units from acceptance. The results apply to the units and scenarios
recorded here.

The WDT1 test uses a dedicated `--wdt-fault-injection` build with a visible
`-wdtfi` version suffix. Its VM hook deliberately hangs after STOP or the
deadline. A read-only check takes a baseline before triggering the reset.
After the physical reset, another read-only check verifies the boot flag,
idle state, files, startup metadata, and that autostart was suppressed.
See [WDT_HARDWARE_ACCEPTANCE.md](WDT_HARDWARE_ACCEPTANCE.md) for the procedure.

### RUN/STOP stress on K210

On 2026-11-21 each board passed two runs of 1,000 cycles. Stock `0.8.0`
firmware passed the RUN/STOP and return-to-menu checks. The
`0.8.0-heapdiag` build repeated the run with Newlib allocator measurements.

| Board | Stock build | Diagnostic build | C heap in use before → after | C heap arena before → after |
| --- | ---: | ---: | ---: | ---: |
| HUSKYLENS SEN0305 (COM10) | 1000/1000 | 1000/1000 | 73,760 → 73,760 bytes | 76,800 → 76,800 bytes |
| Maix Cube (COM11) | 1000/1000 | 1000/1000 | 73,760 → 73,760 bytes | 74,560 → 74,560 bytes |

In every cycle the script printed a ready marker before STOP. The test
verified the next `run_id` and the `STOPPED / EXIT_REQUESTED` state within two
seconds. Control calls to `malloc` and `free` verified the allocator counter
before the run. `mallinfo` reported the same values before the first cycle,
after cycles 1, 10, and 50, and after every 100 cycles. After the run,
BUTTONS opened on both boards and returned to the menu. The diagnostic
build also confirmed that RIGHT and LEFT moved the selection through the
normal button handling.

The SEN0305 runs used HMPY. On Maix Cube, binary HMPY runs stopped after
167, 36, and 590 confirmed cycles because of corrupt responses. One frame
received by the host was missing 62 consecutive bytes. The qualifying Cube
runs used the built-in test script via `HKMPTEST`, `HKMPSTOP`, and
`HKMPSTATUS`.

A follow-up on Cube COM5 reproduced the 62-byte gap after changing the USB
cable. Pacing asynchronous HMPY logs with 2 ms pauses also reproduced it.
The tested mitigation sends encoded stdout and stderr in 32-byte bursts with
20 ms pauses on Cube. Commands and log events retain their existing HMPY frame
formats. This limits the rate of continuous console output on Cube.

With that pacing, Cube completed 1,000 HMPY RUN/STOP cycles in one session.
The wire audit decoded 7,639 frames and verified the complete ready marker
and STOP traceback in all 1,000 runs. Median STOP confirmation was 94 ms and
the maximum was 371 ms. The SEN0305 regression run also completed 1,000 HMPY
cycles with complete output. An earlier SEN0305 attempt received a valid
`BUSY` response during rapid restart. The successful repeat reported zero
retries. Evidence for this follow-up is stored locally under
`build/hardware-acceptance/hmpy-cube-tx-pacing/`.

Reports, per-cycle CSV data, logs, and instrumentation source are stored
locally in `build/hardware-acceptance/micropython-soak/`. Both boards were
restored to stock `0.8.0` firmware. The original autostart setting and file
SHA-256 hashes were verified.

## Main risks

| Risk | Mitigation |
| --- | --- |
| Some revisions may have less than 16 MB of flash | JEDEC allowlist and disable the feature on 8 MB devices |
| VM blocks the interface | Core 1, VM hooks, and bounded bindings |
| A stalled C binding cannot be stopped safely | Timeout and cancellation, then controlled reboot |
| Inter-core cache coherency | Uncached mailbox or explicit cache flush and invalidation, barriers, and hardware stress testing |
| C and GC heaps compete with AI and static buffers | Application exclusivity, static 128 KiB GC heap, map and heap instrumentation |
| Flash wear or corruption | littlefs, atomic rename, upload CRC32, and fault injection |
| Stdout interferes with commands | Exclusive framed session and separate event frames |
| IDE fork drifts from upstream | Thin transport layer, pinned commit, and regular controlled rebase |
| Startup script creates a boot loop | Hold BACK, countdown, and one-boot autostart suppression after watchdog reset |

## Sources

- [MicroPython embed port](https://github.com/micropython/micropython/tree/master/ports/embed)
- [Official embedding example](https://github.com/micropython/micropython/tree/master/examples/embedding)
- [MicroPython v1.28.0](https://github.com/micropython/micropython/releases/tag/v1.28.0)
- [MicroPython porting guide](https://docs.micropython.org/en/latest/develop/porting.html)
- [MicroPython C modules and heap model](https://docs.micropython.org/en/latest/develop/cmodules.html)
- [MicroPython VM hook configuration](https://github.com/micropython/micropython/blob/master/py/mpconfig.h)
- [MaixPy-v1 for K210](https://github.com/sipeed/MaixPy-v1)
- [littlefs design and guarantees](https://github.com/littlefs-project/littlefs)
- [Pybricks Code](https://github.com/pybricks/pybricks-code)
- [Web Serial API limitations](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)
