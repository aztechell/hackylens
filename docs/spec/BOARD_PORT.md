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

- `support=runtime` requires `runtime_profile = "hackylens-full"`;
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
unique except for the one SEN0305 external-connector mux below. Multiple
different functions belonging to one peripheral instance are valid.

`runtime_mux_group` is one narrowly scoped preservation exception for the
existing SEN0305 external connector, whose UART and I2C modes already remap the
same two pins at runtime. The exact group name and complete macro/function/pin/
peripheral set are allowlisted; descriptors cannot invent, move, shrink, or
extend an exception. Every mode is still modeled as explicit routes and
collision-checked. This private metadata is not a general runtime-routing,
switching, arbitration, capability, or discovery API.

The Board Port Contract introduces no runtime route switching or arbitration
API. Firmware builds generate one private `build/generated/board_config.h`
from `board.toml`. That header is build-local and is not a tracked source.

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

## Architecture boundaries and composition

Phase 1 forbids apps from including board/BSP, platform HAL, or K210 SDK
headers. Existing board-independent driver and service APIs MAY remain direct
app dependencies when they contain no board identity, pins, routing, or HAL
assumptions. Removing app-to-driver dependencies is deferred to the Capability
Platform phase.

Runtime needs such as time and boot/recovery use private internal C facades.
They are not versioned public contracts, capabilities, SDK APIs, or discovery
surfaces. Native App Manifests are the sole app-composition input. Transitional
`hackylens.service.legacy-*` declarations preserve only the former Phase 2
driver-availability exclusions; they MUST NOT provide runtime hardware access.
`--require-app` turns such an exclusion into a build error.

## Layout and artifact safety

Flash offsets, partition sizes, and the firmware programming address come from
`board.toml`. Sidecar `flash_layout_sha256` is the SHA-256 of a canonical
in-memory encoding of that flash map. There is no committed flash JSON cache
and no runtime board parser.

Every successful firmware build first writes a canonical private schema-2 build
attestation. It binds the exact image size and SHA-256 to the firmware version,
board/platform/runtime profile, target, build profile, complete enabled/disabled
app composition, diagnostic capability exclusions, generated capability-
inventory SHA-256, board-driven exclusions, and fault-injection state. The
attestation is build metadata, not a public contract or runtime discovery
surface. Only an unmodified `full` target with the complete `hackylens-full`
composition is `release_qualified`; feature-disabled and fault-injection builds
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

The `sipeed-maix-cube` layout and BSP are conformance evidence only. They are
not a runtime-qualified storage contract or evidence of hardware independence.
