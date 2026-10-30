---
contract-id: hackylens.board-port
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# K210 Board Port Contract

This experimental contract defines build-time K210 board selection. It does
not define capabilities, an App SDK, runtime discovery, or runtime resource
arbitration.

## Port package and identity

Every port is stored at `boards/<id>/` and MUST contain `board.toml` and
`board.c`. The descriptor `id` MUST equal the directory name and is the
canonical board identity used in artifacts and HMPY `HELLO.board`. Flash
layout and programming metadata live in the same `board.toml`. Build, package,
and `hkflash` treat that file as the only board source of truth.

The schema-1 support invariants are:

- `support=runtime` requires an allowlisted `runtime_profile`:
  `hackylens-full` or `hackylens-cube`;
- `support=conformance` forbids `runtime_profile` and requires
  `releaseable=false`;
- `releaseable=true` requires `support=runtime`;
- a runtime board's `available` services MUST cover the production hardware
  used by the common startup path;
- a conformance board lists `present` hardware separately from `available`
  driver-supported services;
- unknown schema fields, services, FPIOA functions, and programming values
  are errors.

Service names are private composition metadata. They are not public capability
IDs and MUST NOT be exposed as runtime-discovery surfaces.

## Routes and generated state

Each route independently identifies a physical `pin`, FPIOA `function`, and
`peripheral` instance. Route macros are bound to exact signal functions so
descriptors cannot swap LCD, camera, or SD roles. Pins and functions MUST be
unique except for the external-connector muxes below. Multiple
different functions belonging to one peripheral instance are valid.

`runtime_mux_group` is a narrowly scoped exception for the SEN0305 external
connector (`external-four-pin-mode`, pins 34/35) and Cube Grove connector
(`grove-four-pin-mode`, pins 24/25). Their UART and I2C modes remap the same two
pins through the existing exclusive External Link service. The exact group
name and complete macro/function/pin/
peripheral set are allowlisted; descriptors cannot invent, move, shrink, or
extend an exception. Every mode is still modeled as explicit routes and
collision-checked. This private metadata is not a general runtime-routing,
switching, arbitration, capability, or discovery API.

The Board Port Contract introduces no runtime route switching or arbitration
API. Firmware builds generate one private `build/generated/board_config.h`
from `board.toml`. That header is build-local and is not a tracked source.

The optional private `[drivers]` table selects the exact driver tuple belonging
to the runtime profile. Omitting it retains the original SEN0305 tuple. Cube
explicitly selects the ST7789 octal transport, OV7740, three-way input,
AXP173 management bus and inverted Cube light outputs. Its display has no
FPIOA MOSI route: the LCD octal data bus is the fixed K210 SPI0/DVP path.
Its logical BACK is synthesized from AXP173 power-key events, so no fourth button route
or default is permitted. PMIC I2C1 pins 30/31 are independent of Grove I2C0.
Driver and UI configuration is generated at build time; apps never select a
board or access these routes directly.

The required runtime default `camera_preview_rotate` selects camera mounting
correction for the preview: `0` keeps sensor orientation and `1` rotates it
90 degrees clockwise. Preview overlays use the same transform and viewport;
capture dimensions and published sensor coordinates are unchanged. The shared
AI-input adapter applies the same mounting correction to model pixels when
needed and maps detection rectangles back to sensor coordinates. Cube selects
`1`, SEN0305 selects `0`.

## Camera register transport

A board with driver-supported camera hardware MUST provide `[camera_control]`.
Transport selection is independent of `[drivers].camera`; the latter selects
only the sensor driver. `address_7bit` is always an unshifted address in 1..127.
The removed defaults `camera_sccb_address` and `camera_sccb_hz` are rejected.

- `backend="dvp-sccb"` requires only `address_7bit`, `scl_low_count` and
  `scl_high_count` (counts 1..255). SEN0305 uses address `0x30` and `255/255`.
  Its backend converts the address to the hardware wire representation once.
  These explicit counts preserve the established SDK initialization; frequency
  is unknown, not a claimed 100-kHz measurement.
- `backend="i2c"` requires only `address_7bit`, `controller` and `clock_hz`.
  Cube uses address `0x21`, controller 2 and configured 100 kHz. This is the
  requested configuration, not an electrical measurement. Camera routes MUST
  match that controller, which MUST NOT be routed to another service.

The validator rejects mixed timing modes, absent/mismatched clock/data routes
and unsupported controller values. The existing runtime profiles retain their
exact established transport settings. Build staging includes one backend, or
none when all camera consumers are disabled. Sensor output format belongs to
the driver and is not a board field.

## Board operations

Exactly one selected BSP defines `const hk_board_ops_t hk_board_ops`.
`early_init` is always mandatory and non-NULL. A `*_prepare` callback is
mandatory for every driver-supported service that has a board preparation
hook. Preparation callbacks are limited to power, routing, pinmux, and
electrical preparation. Device drivers and services own device initialization,
operation, cancellation, and lifecycle.

## Programming metadata

`[programming]` is the sole source of board-specific reset, reboot, ISP stub,
flash, baud, retry, and USB-detection defaults. Flash tooling MAY implement
general named reset profiles, but MUST NOT select behavior with board-ID
conditionals or separate board behavior tables. Safe command-line overrides
are accepted only when `programming.supported=true` and cannot bypass layout,
size, address, sidecar, or identity validation.

When `programming.supported=false`, flash commands fail before serial access.
Stub, flash, baud, and retry defaults are forbidden, and USB detection is
`explicit-port`.

Supported programming with `reset_profile="manual"` may omit `reboot_profile`;
then no DTR/RTS reboot fallback is installed. Cube requires an explicit serial
port and selects `sipeed-goe` / `sipeed-goe-normal`, matching the official Sipeed
Cube profile's 100-ms DTR/RTS phases. Automatic BootROM entry and return to the
installed runtime were physically checked on COM11; `--manual` remains an
explicit recovery override.

## Architecture boundaries and composition

Apps must not include board/BSP, driver, platform HAL, or K210 SDK headers.
Bundled apps may use existing portable firmware services under architecture
and feature-boundary checks. Standalone apps use the public SDK.

Time uses the shared immutable typed service. Boot/recovery composition remains
private to firmware. Native App Manifests are the sole app-composition input;
required services are checked against board availability at build time.
`--require-app` turns a board-driven exclusion into a build error.

## Layout and artifact safety

Flash offsets, partition sizes, and the firmware programming address come from
`board.toml`. Sidecar `flash_layout_sha256` is the SHA-256 of a canonical
in-memory encoding of that flash map. There is no committed flash JSON cache
and no runtime board parser.

Every successful firmware build first writes a canonical private schema-2 build
attestation. It binds the exact image size and SHA-256 to the firmware version,
board/platform/runtime profile, target, build profile, complete enabled/disabled
app composition, the SHA-256 of the build-time service selection report,
diagnostic exclusions, board-driven exclusions, and fault-injection state. The
attestation is build metadata, not a public contract or runtime discovery
surface. Only an unmodified `full` target with the complete `hackylens-full`
composition on a board with `releaseable=true` is `release_qualified`;
experimental-board, feature-disabled and fault-injection builds
cannot be relabelled as full artifacts. Standalone image creation and release
packaging require and revalidate the canonical attestation against the exact
input bytes and requested board/profile.

Board-qualified images carry one schema-1 flasher sidecar with the exact required
fields `schema`, `firmware_version`, `board_id`, `platform_id`,
`board_contract_version`, `build_profile`, `image`, `size`, `sha256`,
`flash_address`, and `flash_layout_sha256`; a typed `runtime_profile` is also
present for runtime ports. Packaging verifies both the source sidecar and build
attestation before copying, then revalidates the copied attestation and writes a
new valid same-stem flasher sidecar for the copied release image; release-only
metadata lives in a separate `-release.json` manifest. A missing sidecar is
accepted only with
`--allow-missing-sidecar`, after selected-board address and size checks. The
override never accepts an existing mismatch or an oversized image.

The `sipeed-maix-cube` port is a release-enabled full runtime with all twelve
apps starting with 0.5.0. Its exact-16-MiB layout includes settings slots and
userfs; runtime flash identification must match that capacity before writes.
Full images and release-qualified metadata are published into `dist/` and
packaged with their own board identity. Physical checks and the user's explicit
waiver of external UART/I2C exchange are recorded in [Maix Cube](../MAIX_CUBE.md).
The compile-only negative board lives under
`tests/fixtures/boards/k210-conformance/` and retains conformance safety checks.
