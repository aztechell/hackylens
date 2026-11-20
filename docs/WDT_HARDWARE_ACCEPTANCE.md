# WDT1 hardware acceptance

This procedure verifies the fatal MicroPython STOP fallback on a real K210.
The production Python API intentionally has no operation that can wedge core 1,
so the trigger uses a clearly marked, test-only firmware build. The ordinary
build always compiles the fault-injection branch out.

## Hardware results on 2026-11-20

Both boards passed with `0.8.0-wdtfi`, built from `c57c81d` with the
test-only UART Autostart command described below.

| Board | USB port | Boot banner after STOP | Autostart suppression after STOP | Reports | Restored on `0.8.0` |
| --- | --- | ---: | ---: | --- | --- |
| HUSKYLENS SEN0305 | COM10 | 5.17 s | 7.15 s | `PASS` before and after | Pinned `fireplace.py` autostart |
| Sipeed Maix Cube | COM11 | 5.44 s | 7.38 s | `PASS` before and after | Autostart `OFF` |

Each UART trace recorded `WDT_READY`, the STOP acknowledgment, the new boot
banner, and the explicit WDT1 suppression message on a single connection
with DTR and RTS deasserted. The postchecks reported `boot_flags=1`
(`WDT1_RECOVERY`) and `STOPPED / EXIT_NONE / run_id=0`. The runtime remained
idle during the three-second observation. SHA-256 hashes for both userfs files
matched before and after reset.

On SEN0305, the initial preparation boot launched the pinned user script.
After selecting MicroPython for test autostart, a preparation reboot cleared
the running script before the passing baseline was collected. Neither passing
attempt used an external reset or power cycle between RUN and the postcheck.

The fixture was removed from both devices. A post-flash check confirmed the
original user script digest, the original cleared HMPY startup selection,
and the canonical firmware version on each board. The production boot logs
confirmed their respective restored autostart behavior.

Evidence is stored under `build/hardware-acceptance/wdt1/` in the board
directories `huskylens-sen0305` and `sipeed-maix-cube`. Each contains
`wdt-before.json`, `wdt-after.json`, `wdt-reboot.log`, file digests, the
original settings snapshot, and the production verification files.

## Safety and evidence

- Flashing replaces the installed firmware and requires explicit operator
  approval. Do not add `--erase`; the normal image write preserves userfs.
- The test build reports version `<VERSION>-wdtfi`. Never distribute it as a
  release image: every MicroPython STOP or deadline deliberately wedges core 1.
- Record the original firmware, autostart, startup script, and file inventory
  before the test. Keep separate reports for each board.
- A passing gate consists of both the observed physical reset and the two JSON
  reports below. The postcheck alone does not prove how the reset was caused.

## 1. Build and install the test image

Run these commands from the repository root. For Maix Cube, use
`sipeed-maix-cube` and its detected USB port.

```powershell
$board = "huskylens-sen0305"
$port = "COM10"
$evidence = "build\hardware-acceptance\wdt1\$board"
New-Item -ItemType Directory -Force $evidence | Out-Null
python tools\build_firmware.py full --board $board --wdt-fault-injection
python tools\hkflash.py flash-monitor "build\$board\hackylens-full-wdtfi.bin" --board $board --port $port --flash-baud 921600 --duration 10 --allow-missing-sidecar
```

Create a unique fixture and select it as startup. First note the previous
startup so it can be restored afterwards.

```powershell
@'
print("WDT_READY")
while True:
    pass
'@ | Set-Content -Encoding ascii "$evidence\hka-wdt-fi.py"
python tools\hkpy.py --port $port upload "$evidence\hka-wdt-fi.py" --name hka-wdt-fi.py --startup
```

On the device set **Settings / Autostart / MicroPython**. This proves that a
WDT recovery boot suppresses firmware autostart of the app. The native app does
not execute startup metadata on entry; the fixture is triggered explicitly in
the next section.

For a UART-only setup, `HKWDTSTART` reads the saved Autostart ID and
`HKWDTSTART 10` selects and saves the native MicroPython app. These commands
are available only in the fault-injection build. Record the original ID first.
If the original autostart left a script running, reboot after saving the test
setting and before taking the baseline. The baseline requires a normal boot
with the MicroPython runtime stopped.

Capture the immutable pre-reset evidence. This invocation is read-only:

```powershell
$wdtVersion = ((Get-Content VERSION -Raw).Trim() + "-wdtfi")
python tools\hmpy_acceptance.py --port $port --expected-board $board --expected-version $wdtVersion `
  --report "$evidence\wdt-before.json"
```

The report must be `PASS`, must show `boot_flags=0`, and must name
`hka-wdt-fi.py` as startup.

## 2. Trigger the physical WDT1 reset

Start the fixture explicitly, wait until `WDT_READY` is visible in the device
log, then request STOP:

```powershell
python tools\hkpy.py --port $port run hka-wdt-fi.py --time-limit-ms 30000
python tools\hkpy.py --port $port monitor --seconds 2
python tools\hkpy.py --port $port stop
```

The bounded monitor must contain `WDT_READY`; otherwise do not send STOP and do
not count the attempt as trigger evidence.

The test-only VM hook now spins on core 1. After the two-second STOP grace,
core 0 arms WDT1 and waits; the K210 then physically resets. Record the serial
disconnect/reconnect or boot banner as trigger evidence. For a continuous
capture, use one serial connection with DTR and RTS deasserted. Receive
`WDT_READY` over HMPY, send STOP, then passively record the raw UART output
through the boot banner and the `MicroPython autostart suppressed after WDT1
reset` line. Save the capture as `wdt-reboot.log`. Do not power-cycle or press
RESET between the start of the fixture and the recovery verification.

## 3. Verify the recovery boot read-only

After the recovery boot completes, run:

```powershell
python tools\hmpy_acceptance.py --port $port --expected-board $board --expected-version $wdtVersion `
  --verify-wdt-recovery `
  --wdt-baseline "$evidence\wdt-before.json" `
  --report "$evidence\wdt-after.json"
```

The postcheck requires `WDT1_RECOVERY`, `STOPPED / EXIT_NONE / run_id=0`, the
same firmware/board/files/startup as the baseline, and three seconds with no
run. It rejects FORMAT, workflow, or reconnect suites so this phase cannot
mutate the recovery evidence.

Finally restore the previous Autostart and startup selections, delete only the
test fixture, and flash a normal image built without `--wdt-fault-injection`.
Verify the canonical version without `-wdtfi`, the original autostart behavior,
and the original files and startup metadata.
