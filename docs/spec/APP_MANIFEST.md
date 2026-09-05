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

Build tooling parses every production manifest once per firmware build, expands
short service names through one build-time mapping, and emits an immutable
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
| `lifecycle` | yes | `legacy` or `v2`; retained until the remaining production apps share one lifecycle |
| `entry` | yes | C symbol of the typed lifecycle entry object; it is not derived from `id` because existing bindings are not a uniform `{id}_{lifecycle}_entry` convention |
| `sources` | yes | non-empty array of app-relative C/C++ translation units |
| `requires` | yes | short names of required capabilities and/or build-time services; may be empty |
| `optional` | no | non-empty short-name array of optional capabilities with named fallbacks |
| `private_includes` | no | non-empty app-relative private include directories; omit when empty |
| `menu_order` | no | positive uint16 menu order; omit to hide the app |
| `autostart_id` | no | stable uint16 autostart identity; omit or `0` means ineligible |
| `tick_ms` | yes | positive tick period in milliseconds, at most 60_000 |
| `debug` | no | `HKHELP` command text when the app has a debug handler; omit otherwise |

Identity MUST NOT depend on directory enumeration, source order, object order,
link order, menu order, or array position. App IDs, entry symbols, visible menu
order values, and every non-zero autostart ID are collision-checked before
compile. The generated descriptor symbol is `hk_generated_app_<id>` with hyphens
mapped to underscores.

Canonical tokens use lowercase ASCII kebab form. An app ID starts with a
lowercase ASCII letter and then uses lowercase letters, digits, and single
hyphen-separated non-empty components. `entry` uses ordinary C identifier
syntax. For `lifecycle = "legacy"`, `entry` names one app-owned immutable
`hk_legacy_app_entry_t`. For `lifecycle = "v2"`, it names one immutable
`hk_app_v2_entry_t`. The generator never guesses callback symbol names.

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

`requires` and `optional` use short names. One build-time mapping expands them
to existing Capability API IDs or transitional legacy service IDs:

| Short name | Expansion |
| --- | --- |
| `display` | `hackylens.cap.display` |
| `input` | `hackylens.cap.input` |
| `time` | `hackylens.cap.time` |
| `lights` | `hackylens.cap.lights` |
| `external-link` | `hackylens.cap.external-link` |
| `camera` | `hackylens.service.legacy-camera` |
| `sd-card` | `hackylens.service.legacy-sd-card` |
| `internal-flash` | `hackylens.service.legacy-internal-flash` |
| `settings` | `hackylens.service.settings` |

Unknown names are errors. The same name cannot be both required and optional.
Optional capabilities have a fixed named fallback from the same mapping:
`display` → `headless`, `external-link` → `hide-external-link-menu`. Services
cannot be optional. Transitional `legacy-*` services require `lifecycle =
"legacy"` and remain build-only exclusions; they do not generate SDK handles or
raw hardware access.

Capability requests keep instance `0` and the current `[0.1.0, 0.2.0)` range.
Feature bits stay in the mapping, not in `app.toml`, because they exist only to
preserve the current grant/composition ABI. Per-app exceptions in that mapping
are limited to proven runtime acquire paths:

- `time` adds `sleep-until` for CAMERA, APRILTAG, and MICROPYTHON;
- `lights` uses illumination+RGB for camera-family apps;
- `external-link` uses UART+I2C-controller for MICROPYTHON.

SETTINGS and SLEEP do not declare `lights`. They apply persisted brightness
through the existing `consumer:settings-lights` service. SETTINGS does not
declare `external-link`; UART/I2C menu items still reconfigure
`consumer:external-link-service`. Lifecycle-v2 preflight acquires every
available declaration, so those persistent exclusive/channel leases would
otherwise return `HK_ERR_BUSY` and the app would fail to open.

A missing required capability excludes the app; an explicit require-app build
request turns that exclusion into a build error. Combined required and optional
capability requests are limited to 16; services are limited to 16.

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
autostart identity, lifecycle kind and typed entry reference, debug text, the
tick interval derived from `tick_ms`, expanded capability/service requests, and
the menu presentation hook `{id_with_hyphens_as_underscores}_draw_icon`. That
icon symbol is a descriptor field, not a lifecycle callback. `lifecycle` and
`entry` remain the temporary build-time boundary between the v2 model and the
one private legacy adapter.
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
