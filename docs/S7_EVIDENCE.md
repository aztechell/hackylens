# S7 verification — 2026-09-06

Status: accepted for S7. Firmware commit `0405a09` passed 232 host tests and
[normal-push CI](https://github.com/aztechell/hackylens/actions/runs/34034164841).
The user confirmed QR and Sleep, and explicitly accepted FILES/GIF despite
heavy GIFs still playing below nominal speed.

All 12 apps use one typed entry. The lifecycle selector, legacy entry union,
adapter, and registry background/media iteration are removed. Only explicitly
addressed debug handlers run. Pending KPU/core1 completion uses two fixed slots,
retaining detector epochs and job tickets. MicroPython VM polling remains an
explicit service; UI uses active TIMER and INPUT events. API v1/HMPY unchanged.

## Reproduced defects

- QR black preview: monotonic-only request avoids an undeclared sleep feature.
- QR exit: UART recorded successful decode in 1,276,190 us, followed by
  `poll failed result=-11`; the budget was 1,000,000 us. It is now 3,000,000 us,
  independent of the 20 ms polling cadence.
- GIF deadlines now include decoding time and need no extra poll to arm a delay.
  Runtime schedules the next tick from callback start, without a catch-up loop.
- Null render avoids competing Display transactions for existing media services.
  A busy Display retains pending invalidation.

## Resource comparison

An isolated build of commit `b76ebbb37864bc7f81d808fb19c02a319f24cd52`
with the same pinned dependencies measured 1,566,968 raw image bytes and
2,893,352 static RAM bytes. Artifact hashes are in the
[S7 baseline](baselines/s7-resources.json).

The validated full build measured 1,566,200 image bytes and 2,898,472 static
RAM bytes: image -768 bytes, RAM +5,120 bytes. This is exactly five new fixed
1024-byte app state slots. Flash erase-block occupancy is unchanged at
1,568,768 bytes.

`check_s7_resources.py` enforces no raw-image growth, at most 6 KiB RAM growth,
and no new direct runtime allocation/task/queue/core creation sites. The old
Phase 3 comparison predates QR changes already in the starting commit; its
historical evidence is retained. CI now uses the measured S7 baseline.

## Verification

231 host tests passed, including GIF pixels/deadlines/loop/pause, pending-only
resource cleanup, timer cadence with a callback longer than its interval, and
no Display batch for null render. Full and MicroPython-disabled builds passed architecture, linked composition
and feature-symbol checks. The disabled image is 1,372,024 bytes. SDK, manifest,
capability, board, environment and documentation gates passed.

Host dispatch p99 measured 7 ns for event, 66 ns for launch, and 103 ns for
stop (101 samples, 1000 iterations each, pinned GCC 13.2.0). These are host
runtime overhead measurements, excluding callback bodies and hardware I/O.

SEN0305 UART smoke passed sequential open/ping/menu for Buttons, Pong,
Terminal, Settings, Files, Camera, Face Detect, Object Detect, AprilTag and
MicroPython. Models loaded and camera frames continued after switching.
HKMPTEST started run 1, HKMPSTOP produced `state=0 exit=2` and the expected
KeyboardInterrupt output. QR preview subsequently ran at about 18.6 fps.
This smoke does not prove QR recognition, GIF playback, button interaction,
or long-running AI correctness.

## Acceptance scope and known limitation

QR and Sleep are user-confirmed. The user accepted the improved FILES/GIF
behavior for S7 while explicitly reporting that heavy GIFs still do not reach
full playback speed. This remaining performance limitation is accepted and
does not block S7. Button events are retained during decoding, but their
handling waits until the current frame ends. Further heavy-GIF optimization
is separate follow-up work, not claimed as completed here.

The requested combined repair migrated apps in one working tree. Acceptance
uses the recorded UART smoke and user checks; it does not claim separate
hardware-accepted commits per wave, exhaustive physical BACK testing, or
long-run qualification of every AI path.

Implementation commit: `4b7742f`. Local working-tree and staged whitespace
checks passed. The first push was blocked by automatic approval review.
The user then explicitly authorized the existing GitHub remote and branch;
implementation and evidence were pushed successfully. The initial verification
run is [GitHub Actions 34032859242](https://github.com/aztechell/hackylens/actions/runs/34032859242).
Hardware acceptance remains separate from the CI result.

## FILES input follow-up

The user confirmed QR works. They reported excessively fast LEFT/RIGHT hold
scrolling and unreliable buttons on heavy GIFs. Hold repeat still used tick
counts after FILES cadence changed to 20 ms. It now uses 500 ms initial delay
and 180 ms subsequent intervals, without catch-up after slow frames.

GIF row presentation now samples the injected Input handle into the existing
debounced event ring. It never consumes events or dispatches app callbacks
inside the frame transaction. A short press/release during one slow frame is
retained for normal foreground dispatch. The binding is removed on app stop.
The response still waits for the current frame; this is not an incremental
decoder or a guarantee of sub-frame input latency.

Regression tests exercise repeat cadence, delayed ticks, release cancellation,
and a complete BACK press/release during a simulated 200 ms frame using the
production Input debounce/event ring. The user accepted this follow-up with
the heavy-GIF performance limitation recorded above.

Follow-up validation: 232 host tests passed. Full firmware, SDK, generated/object
architecture, linked composition, and S7 resource gates passed. Raw image is
1,566,392 bytes; static RAM remains 2,898,472 bytes. The follow-up was flashed
to COM10. Normal-push CI passed for `0405a09`, including both firmware profiles,
and the user accepted FILES/GIF. No further firmware change was made to close
the documentation status.
