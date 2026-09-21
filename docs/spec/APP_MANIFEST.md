---
contract-id: hackylens.native-app-manifest
owner: platform-architecture
version: 0.1.0
stability: experimental
phase: 3
schema-major: 1
format-scope: native-app-build
runtime-parsed: false
compatibility-app-runtime: >=0.2.0,<0.3.0
compatibility-capability-api: >=0.1.0,<0.2.0
---

# HackyLens Native App Manifest

## Purpose and authority

`app.toml` is the build-time contract for one statically linked native feature
app. It is the sole source of app identity, source inclusion, required service
presence, menu visibility/order, stable autostart identity, and tick period.

Build tooling parses every production manifest once per firmware build, checks
short service names against board service availability, and emits an immutable
registry into the build directory. Firmware receives only those generated
descriptors. Firmware MUST NOT contain a TOML parser, filesystem app discovery,
runtime registration, mutable descriptor construction, or dynamic native-code
loading.

The semantic contract is `0.1.0 experimental`. Unknown fields are errors.

## Manifest fields

Every production manifest declares:

| Field | Required | Meaning |
| --- | --- | --- |
| `id` | yes | lowercase kebab app ID, at most 63 UTF-8 bytes |
| `name` | yes | non-empty trimmed display text, at most 96 UTF-8 bytes |
| `entry` | yes | C symbol of the typed lifecycle entry object; it is not derived from `id` because entry symbols are explicit |
| `sources` | yes | non-empty array of app-relative C/C++ translation units |
| `requires` | yes | short names of required capabilities and/or build-time services; may be empty |
| `optional` | no | non-empty short-name array of optional hardware services with named fallbacks |
| `private_includes` | no | non-empty app-relative private include directories; omit when empty |
| `menu_order` | no | positive uint16 menu order; omit to hide the app |
| `autostart_id` | no | stable uint16 autostart identity; omit or `0` means ineligible |
| `tick_ms` | yes | positive tick period in milliseconds, at most 60_000 |
| `tick_budget_ms` | no | callback budget, 1–5000 ms; defaults to `tick_ms`, independent of cadence |
| `debug_entry` | no | C symbol of the explicitly addressed diagnostic handler |
| `debug` | no | `HKHELP` command text when the app has a debug handler; omit otherwise |

Identity MUST NOT depend on directory enumeration, source order, object order,
link order, menu order, or array position. App IDs, entry symbols, visible menu
order values, and every non-zero autostart ID are collision-checked before
compile. The generated descriptor symbol is `hk_generated_app_<id>` with hyphens
mapped to underscores.

Canonical tokens use lowercase ASCII kebab form. An app ID starts with a
lowercase ASCII letter and then uses lowercase letters, digits, and single
hyphen-separated non-empty components. `entry` uses ordinary C identifier
syntax. `entry` names one immutable `hk_app_v2_entry_t`. The obsolete
`lifecycle` field is rejected. The generator never guesses callback symbols.

Omitting `menu_order` hides the app. Visible apps keep unique positive orders so
enabling a hidden app cannot silently reorder another entry. An autostart-eligible
app has a non-zero stable uint16 ID; SETTINGS and SLEEP omit the field and stay
ineligible. Persistence accepts only OFF or exact reserved-set membership.

All declared paths use `/`, contain only canonical relative components, exist
with exact filesystem case, have the required file/directory kind, and resolve
inside the real manifest directory. Drive-qualified, UNC, absolute, `.`/`..`,
backslash, missing, case-aliased, symlink/junction escape, and wrong-suffix paths
are rejected before compilation.

## Required services

`requires` and `optional` use short service names directly. Hardware bindings
are `time`, `input`, `display`, `lights` and `external-link`. Existing firmware
service requirements are `camera`, `sd-card`, `internal-flash` and `settings`.
There is no expansion into generic capability requests, instance numbers,
version ranges, feature masks or owner grants.

Unknown names and a name appearing in both lists are errors. Optional hardware
services have fixed fallbacks: `display` → `headless`, `external-link` →
`hide-external-link-menu`. Firmware services cannot be optional. Requirements
control build inclusion; they neither inject SDK handles nor authorize raw
hardware access. A missing required service excludes the app; `--require-app`
turns that exclusion into an error.

Persistent settings, camera light control and the native external-link service
keep their own typed sessions. App-scoped sessions live in runtime, and
MicroPython sessions last until worker terminal handoff. Build requirements do
not acquire these resources or create competing claims during app startup.

## Canonical model and command

`python tools/check_app_manifests.py --scan-root <directory>` recursively
validates every `app.toml` below the directory and emits canonical UTF-8 JSON.
`--output <path>` writes the same bytes to a file. An empty input is an error.
The model records only scan-root-relative directories and manifest-relative
paths, never workspace absolute paths. Apps sort by ID; source/include paths,
expanded capabilities, features, and services use stable explicit keys.
Identical input trees at different host paths produce byte-identical canonical
models.

The command is a pre-compile build gate. Firmware does not read TOML or this
host JSON model.

## Generated composition

One validated canonical model generates:

- source/include build composition and `HK_ENABLE_APP_*` definitions in
  `hk_config.h`;
- one immutable registry, `build/generated/app_registry/registry.{h,c}`, copied
  into the firmware stage for compilation.

No separately maintained production app table may duplicate those facts.
Committed `firmware/generated/app_registry/*`,
`firmware/generated/app_composition/composition.json`, and
`firmware/config/app_config_defaults.h` are not sources of truth. Generated
output is deterministic and ordered by explicit stable keys. Descriptors are
read-only for the entire boot and expose no board routes, pins, peripheral
instances, provider vtables, drivers, or HAL objects.

The generated descriptor contains identity, menu visibility and order, stable
autostart identity, typed entry reference, debug text and handler, the
tick interval derived from `tick_ms`, expanded capability/service requests, and
the menu presentation hook `{id_with_hyphens_as_underscores}_draw_icon`. That
icon symbol is a descriptor field, not a lifecycle callback. There is one
entry type and no legacy adapter. Tick cadence and callback budget are separate
descriptor limits.
A canonical descriptor array is ordered by app ID; the separate menu view is
ordered only by explicit `menu_order`. Conditional build flags remove a disabled
descriptor and its entry reference without renumbering persisted autostart IDs.
The generator also emits an immutable reserved-ID set of all non-zero uint16
autostart IDs before applying app enable flags. Enabled autostart choices are
enumerated from the canonical descriptor array, independently of menu visibility
and menu order. A disabled app has no runtime target or sources but keeps its
reserved persisted identity. Settings schema v5 stores the full uint16 identity;
loading schema-v4 uint8 IDs 0–10 zero-extends them without renumbering.

`python tools/gen_app_composition.py --check` validates the production manifest
set and proves generation is deterministic. It does not compare against
committed generated copies. `python tools/check_app_composition.py
--verify-build <board>` checks the registry artifact actually written into the
build directory and staged firmware.

Composition discovers app ownership only from the recursively validated
canonical model. It treats `.c`, `.cc`, `.cpp`, and `.cxx` uniformly and rejects
every app-package production translation unit without a manifest owner. For each
enabled app, only its app root and directories explicitly named by
`private_includes` MAY become private compiler include roots.

## Native app manifest versus Phase 4 Project Format

The native app manifest describes C sources compiled into one firmware image.
It is not a developer project package, device filesystem object, installed
program, Python runtime selection, asset synchronization format, or executable
discovery record. Those concerns belong to the future Phase 4 Project Format.

## Compatibility

Native App Manifest contract `0.1.x` is accepted with App Runtime `0.2.x` and
Capability API `0.1.x`. Experimental App Runtime consumers request
`[0.2.0, 0.3.0)`. Changing one version axis does not implicitly change the
others.

## References

- [App Runtime](APP_RUNTIME.md)
- [Feature App SDK](APP_SDK.md)
- [Capability API](CAPABILITY_API.md)
- [Versioning Policy](VERSIONING.md)
- [Architecture Vision](../ARCHITECTURE_VISION.md)
- [Roadmap](../ROADMAP.md)
- [ADR-0008](../adr/0008-generate-native-app-composition.md)
