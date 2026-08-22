# Phase 2.13 SEN0305 hardware acceptance

This procedure qualifies the five Phase 2 capabilities on one exact
HUSKYLENS/SEN0305 firmware image. It does not qualify Maix Cube and it does not
transfer hardware status to a later build.

Current status: **NOT RUN**. No SEN0305 serial device or external-link fixture
was available when this procedure and its evidence validator were added. This
document is a runbook, not physical evidence.

## Non-negotiable gate

A passing session needs all of the following at the same time:

- one immutable `phase2-candidate-result.json` copied from a green Phase 2
  rolling result;
- the exact full-profile image named by that result, with matching image,
  composition, capability-inventory, and attestation hashes;
- the candidate commit and its green `Release firmware` workflow URL;
- the pinned Kendryte compiler archive/version and SDK revision, plus the host
  Python and CMake versions used for the candidate build;
- one recorded SEN0305 board serial/revision and operator identity;
- a fixture with an ID and a checked-in schematic, providing both UART TX/RX
  loopback and a known 7-bit I2C target;
- raw logs and timing logs under `docs/evidence/phase2-hardware/`;
- every required result marked `pass` in
  `docs/evidence/phase2-hardware-smoke.json`;
- a successful `python tools/check_phase2_hardware.py` invocation.

Host tests never replace the external electrical fixture. A missing UART
loopback or known I2C target leaves Phase 2.13 open.

The firmware version is part of the qualified identity. In particular, a
later Phase 2.14 change from the current development version to `0.4.0` creates
a different image and invalidates an earlier smoke. Select the final-version
candidate before the physical run, or repeat the complete physical run on the
new exact image.

## 1. Select the candidate

Start from a clean candidate commit. Run the complete automated Phase 2 gate,
including both SEN0305 profiles, capability-absent diagnostics, Cube compile
conformance, architecture evidence, shared fake/K210 suites, and resource
evidence. The push workflow must be green.

After that exact result is green and before changing any file in its source
scope, make the immutable candidate snapshot:

```powershell
Copy-Item -LiteralPath docs\evidence\phase2-result.json `
  -Destination docs\evidence\phase2-candidate-result.json
Get-FileHash -Algorithm SHA256 `
  -LiteralPath docs\evidence\phase2-candidate-result.json
git rev-parse HEAD
```

Record the full 40-character commit and the workflow run URL. From
`builds.profiles.full` in the candidate result, record these fields without
retyping or rounding them:

- `image.raw_bytes` and `image.sha256`;
- `composition_sha256`;
- `capabilities_sha256`;
- `attestation_sha256`.

Record the pinned compiler archive SHA-256, compiler version, and SDK revision
from `docs/evidence/phase2-baseline.json`. Record the actual host Python and
CMake versions used for the candidate build. The validator rejects a pinned
compiler or SDK identity that differs from the Phase 2 baseline.

The local `build/huskylens-sen0305/hackylens-full.bin` must have the same byte
count and SHA-256 before it is flashed. The firmware version recorded in the
hardware report must equal the repository `VERSION` file.

Do not edit `phase2-candidate-result.json` after selection. If automated
evidence, firmware source, composition, toolchain, or `VERSION` changes, delete
the unqualified candidate snapshot and select a new one. Never edit a candidate
that already has physical evidence referring to it.

## 2. Record the board, operator, and fixture

Record a stable board serial/asset ID, visible hardware revision, operator
identity, and UTC timestamp. A host COM port is transient and is not a board
identity.

Give the fixture a stable ID and check its schematic or wiring record into
`docs/evidence/phase2-hardware/`. The SEN0305 Board Port defines the shared
external connector routes as:

| Mode | Signal | K210 IO |
|---|---|---:|
| UART | RX | 34 |
| UART | TX | 35 |
| I2C | SCL | 34 |
| I2C | SDA | 35 |

For the UART portion, connect the external connector TX route to its RX route.
For I2C controller qualification, connect a deterministic target with a known
identity and 7-bit address. Record the address in both the fixture and I2C
result sections. Use the board vendor's physical connector pinout to identify
power and ground; the logical IO table above is not a power-pin diagram. Verify
compatible logic voltage, common ground, and suitable I2C pull-ups before
powering the fixture.

## 3. Flash the exact image

Flashing replaces the installed firmware and therefore requires operator
approval. Preserve USERFS: do not add `--erase`.

```powershell
python tools\hkflash.py flash-monitor `
  build\huskylens-sen0305\hackylens-full.bin `
  --board huskylens-sen0305 --port COM10 --duration 10
```

Replace `COM10` with the currently detected port. Save the complete preflight,
flash, boot, and HMPY HELLO output as a raw log. Confirm that the booted version,
board ID, and image candidate agree before starting observations.

## 4. Physical checks

Use UTC timestamps and a monotonic host/instrument clock in raw logs. Retain
the unprocessed samples; summaries alone are not evidence.

### Buttons and Time

Exercise Left, OK, Right, and Back separately. For each button capture press,
release, and a sustained hold. Verify one edge per accepted transition and zero
repeated edges during the hold. Record sample count plus min, p99, and max for
debounce latency and delivered-event latency.

Record the same statistics for monotonic-read overhead, bounded sleep latency,
and cooperative cancel latency. Confirm that the monotonic value never moves
backwards during the session.

### Display

Observe the menu and every native view touched by Phase 2. Exercise camera and
files full-frame presentation, Pong dirty frames, and the MicroPython overlay.
For MicroPython, cover success, cancel, retry, cleanup, and restoration of the
latest native screen.

Record full-present samples; every measured full present must be at most
`500000 us`. Compare the same instrumented workload with its candidate
baseline; regression must be at most `10%`. Save timing logs and screen/frame
captures needed to identify the exercised views.

### UART and I2C

With the UART loopback installed, transmit a non-trivial, byte-distinct payload
and verify exact received bytes. Observe completion only after the final byte
has drained onto the wire. Cancel a longer transfer and keep observing past the
deadline; record zero late bytes.

Move to the documented I2C target. Exercise controller write, read, and a
combined prefix/read transaction. Force one bounded NACK or equivalent fixture
error, then prove recovery. Switch UART to I2C and back without rebooting.

Run the normal external/HMPY service before MicroPython takes the connector.
After Python success, cancellation, and error cleanup, prove that the configured
native transport is restored and complete at least one HMPY request/response.

### Lights and regression

Observe backlight, illumination, and RGB. Exercise normal completion,
cancellation/error cleanup to safe-off, and restoration of persisted settings.

On the same exact image, pass boot, camera capture, SD read/write/delete, Files
decode and frame-pool reuse, settings persistence, Sleep, and HMPY. Do not
format USERFS as part of the acceptance run.

## 5. Evidence and validation

Create `docs/evidence/phase2-hardware-smoke.json` as canonical JSON (UTF-8,
sorted keys, two-space indentation, trailing newline). The validator defines
the exact field set and rejects unknown fields. Its eight required result IDs
are:

- `buttons`;
- `display`;
- `uart`;
- `i2c`;
- `external_service_restore`;
- `lights`;
- `time`;
- `regression`.

Every result must reference one or more declared artifacts. The artifact list
must include at least one `fixture-schematic`, one `raw-log`, and one
`timing-log`; every path must stay under
`docs/evidence/phase2-hardware/` and its content SHA-256 must match.

Use [the validator](../tools/check_phase2_hardware.py) and its deterministic
negative fixtures as the schema reference:

```powershell
python tools\check_phase2_hardware.py `
  --evidence docs\evidence\phase2-hardware-smoke.json
python -m unittest -v tests.test_phase2_hardware
```

The release workflow automatically runs the hardware validator once the actual
hardware-smoke file exists. Keep Phase 2.13 `in_progress` until the evidence
commit itself is green. Only then may a separate documentation-only commit mark
all Phase 2.13 checklist items complete.

## Rollback and cleanup

Restore temporary settings and remove only files created for the session. Keep
the candidate result, smoke report, raw logs, timings, and fixture record
immutable.

Any later binary, including one produced only by a version change, has no
hardware-qualified status. It needs a new candidate selection and a complete
new smoke rather than an edited copy of the old report.
