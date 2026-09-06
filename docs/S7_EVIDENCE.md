# S7 verification — 2026-09-06

Status: in progress; not hardware accepted or CI accepted yet.

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

## Acceptance still needed

- Same physical QR stays in result view; GIF speed verified on SEN0305.
- Physical Sleep/wake and interactive BACK checks.
- Normal-push CI. Historical runs do not qualify the current diff.

The requested combined repair migrates apps in one working tree rather than
claiming separate hardware-accepted commits per wave. S7 stays open until
hardware and CI evidence exists.

Implementation commit: `4b7742f`. Local working-tree and staged whitespace
checks passed. Normal-push CI has not run: automatic approval review blocked
`git push origin phase-3-work` pending explicit authorization to send the
commit to the existing GitHub remote. The commit remains local.
