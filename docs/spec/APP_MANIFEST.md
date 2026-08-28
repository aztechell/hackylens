---
contract-id: hackylens.native-app-manifest
owner: platform-architecture
version: 0.1.0
stability: experimental
phase: 3
schema-major: 1
format-scope: native-app-build
runtime-parsed: false
compatibility-app-runtime: >=0.1.0,<0.2.0
compatibility-capability-api: >=0.1.0,<0.2.0
---

# HackyLens Native App Manifest

## Purpose and authority

`app.toml` is the build-time contract for one statically linked native feature
app. The manifest is the sole source of app identity, source inclusion,
lifecycle entry metadata, capability and service declarations, menu/autostart
metadata, resource limits, help/debug metadata, and test composition metadata.

Build tooling parses and validates the file before compilation and emits one
canonical model. Firmware receives only immutable generated descriptors and
tables. Firmware MUST NOT contain a TOML parser, filesystem app discovery,
runtime registration, mutable descriptor construction, or dynamic native-code
loading.

The initial semantic contract is `0.1.0 experimental`; its independently
encoded manifest schema major is `1`. Unknown schema values and unknown fields
are errors. Schema 1 field grammar and canonicalization are defined below.

## Required properties

Every manifest declares explicit, stable values for:

- canonical app ID, display name, app version, and entry symbol;
- lifecycle kind (`legacy` adapter or lifecycle `v2`);
- source and private include paths relative to the app directory;
- menu inclusion/order and a stable autostart ID where eligible;
- required capabilities and optional capabilities with named fallbacks;
- app-scoped service namespaces;
- finite state, static RAM, stack expectation, tick, and render limits;
- host tests and build-composition test metadata.

Identity MUST NOT depend on directory enumeration, source order, object order,
link order, menu order, or array position. App IDs, entry symbols, autostart IDs,
menu order values, and generated symbols are collision-checked before compile.

Required capability versions use inclusive minimum and exclusive maximum
bounds plus required feature bits, matching the Capability API. An optional
requirement without an explicit named fallback is invalid. A missing required
capability excludes the app; an explicit require-app build request turns that
exclusion into a build error.

All resource values are finite positive integers within platform limits.
Zero, negative, overflowed, sentinel-as-unbounded, or omitted identity/resource
policy is invalid. Paths are resolved against the real app directory and MUST
NOT escape it through `..`, absolute paths, symlinks, junctions, case folding,
alternate separators, or equivalent path tricks.

## Schema 1 grammar

The root is one TOML table with exactly these fields; every field is required
and there are no implicit defaults:

| Field | Schema 1 value |
| --- | --- |
| `schema` | integer `1` |
| `id` | lowercase kebab ID, at most 63 UTF-8 bytes |
| `name` | non-empty trimmed display text, at most 96 UTF-8 bytes |
| `version` | canonical SemVer with no leading-zero numeric identifiers |
| `entry` | C symbol naming the typed lifecycle entry object |
| `generated_symbol` | C symbol reserved for the generated descriptor |
| `lifecycle` | exactly `legacy` or `v2` |
| `sources` | non-empty array of app-relative C/C++ source files |
| `private_includes` | explicit array of app-relative private include directories; it may be empty |
| `menu` | table with explicit `visible` boolean and positive uint16 `order` |
| `autostart` | table with explicit `eligible` boolean and uint16 `id` |
| `capabilities` | table with explicit `required` and `optional` request arrays |
| `services` | explicit array of app-scoped service declarations; it may be empty |
| `limits` | complete finite resource and scheduling policy table |
| `metadata` | table with required `help` and `debug` strings; exact bounds below |
| `tests` | host-source and build-composition test metadata |

Canonical app and requirement tokens use lowercase ASCII kebab form. An app ID
starts with a lowercase ASCII letter and then uses lowercase letters, digits,
and single hyphen-separated non-empty components; capability IDs add the
`hackylens.cap.` prefix and service IDs add `hackylens.service.`. `entry` and
`generated_symbol` use ordinary C identifier syntax. For `lifecycle =
"legacy"`, `entry` names one app-owned immutable `hk_legacy_app_entry_t`
binding object containing that app's existing callbacks and screen adapter; for
`lifecycle = "v2"`, it names one immutable `hk_app_v2_entry_t` lifecycle object.
The generator emits typed references to those objects and never guesses callback
symbol names. Every menu order is
unique, including for a non-visible entry, so enabling it cannot silently
reorder another app. An autostart-eligible app has a non-zero stable uint16 ID;
an ineligible app explicitly uses ID zero. The validator collision-checks app
ID, entry, generated symbol, menu order, and every non-zero autostart ID across
the complete input set.

`metadata.help` and `metadata.debug` are required non-empty trimmed UTF-8
strings without control characters. Each is limited independently to at most
1024 UTF-8 bytes.

Each capability request contains exactly:

```toml
id = "hackylens.cap.input"
instance = 0
minimum = "0.1.0"
maximum_exclusive = "0.2.0"
features = ["events", "state"]
```

Capability request versions are canonical numeric `MAJOR.MINOR.PATCH` values
whose components fit `hk_version_t`. `minimum` is inclusive, precedes the
exclusive maximum, and the same `(id, instance)` cannot occur twice or be both
required and optional.
The feature array is explicit, duplicate-free, and may be empty. An optional
request has one additional required lowercase-kebab `fallback` field. This is
the same range and required-feature-set model as `hk_capability_request_t`; the
validator does not replace missing bounds with a current provider version.

Each service declaration contains exactly `id` and `namespace`. A namespace is
one or more lowercase-kebab components below `<app-id>.`, which makes service
state app-scoped before any service handle is generated. Schema validation does
not assert that a selected board provides the service; build composition makes
that decision later.

Until the corresponding public service/capability migration exists, a
`lifecycle = "legacy"` app MAY declare a transitional build-only service named
`hackylens.service.legacy-<driver-kind>`. It preserves an existing Phase 2
driver-availability exclusion while the legacy implementation remains linked.
Such a declaration MUST NOT generate an SDK/runtime handle, grant raw hardware
access, or publish a future Camera/Storage API. It is forbidden for lifecycle
`v2`; migration removes it in favour of the versioned public capability or
app-scoped service. This compatibility form is the only representation of the
former private `firmware/app_requirements.toml` `requires` values.

The `limits` table contains exactly these positive integers:

| Field | Portable schema-1 ceiling | Additional rule |
| --- | ---: | --- |
| `static_ram_bytes` | 8,388,608 | includes fixed app state and other app-owned static data |
| `stack_bytes` | 32,768 | stack expectation, not a request for a new task |
| `state_bytes` | 1,048,576 | cannot exceed `static_ram_bytes` |
| `tick_interval_us` | 60,000,000 | finite positive interval |
| `tick_budget_us` | 1,000,000 | cannot exceed `tick_interval_us` |
| `render_budget_us` | 1,000,000 | finite positive render budget |

Zero, negative, boolean, string sentinel, overflow, omitted, and above-ceiling
values are invalid. These are schema ceilings, not reservations and not
permission to exceed a selected board/build profile budget. The values neither
create tasks nor make a timeout infinite.

`tests.host_sources` is a non-empty array of app-relative `.c`, `.cc`, `.cpp`,
`.cxx`, or `.py` files. `tests.build_profiles` is a non-empty duplicate-free
subset of `standalone`, `full`, and `disabled`. Test metadata is composition
input only; it is not runtime discovery metadata.

All declared paths use `/`, contain only canonical relative components, exist
with exact filesystem case, have the required file/directory kind, and resolve
inside the real manifest directory. Drive-qualified, UNC, absolute, `.`/`..`,
backslash, missing, case-aliased, symlink/junction escape, and wrong-suffix paths
are rejected before compilation.

## Canonical model and command

`python tools/check_app_manifests.py --scan-root <directory>` recursively
validates every `app.toml` below the directory and emits canonical UTF-8 JSON.
`--output <path>` writes the same bytes to a file. An empty input is an error.
The model records only scan-root-relative directories and manifest-relative
paths, never workspace absolute paths. Apps sort by ID; source/include/test
paths, capabilities, features, services, and profile names use stable explicit
keys. Consequently identical input trees at different host paths produce
byte-identical canonical models.

The command is a pre-compile build gate. Firmware does not read its TOML input
or this host JSON model; later generation consumes the validated in-memory
model and gives firmware immutable descriptors only.

## Generated composition

One validated canonical model generates:

- source/include build composition and app enable definitions;
- immutable registry descriptors and lifecycle adapter binding;
- capability/service grants and incompatibility diagnostics;
- menu, stable autostart, help, debug, and resource metadata;
- deterministic host/full/disabled test matrix inputs.

No separately maintained production app table may duplicate those facts.
Generated output is deterministic, ordered by explicit stable keys, and checked
for freshness. Descriptors are read-only for the entire boot and expose no
board routes, pins, peripheral instances, provider vtables, drivers, or HAL
objects.

The private generated descriptor contains manifest identity/version,
menu visibility and order, stable autostart identity, lifecycle kind and typed
entry reference, help/debug text, finite limits, and capability/service request
metadata. A canonical descriptor array is ordered by canonical app ID; the
separate menu view is ordered only by explicit `menu.order`. Conditional build
flags remove a disabled descriptor and its entry reference without renumbering
persisted autostart IDs. Empty and single-app compositions remain bounded const
tables rather than runtime registration special cases.

`python tools/gen_app_composition.py --check` validates the production manifest
set, recomputes the source/include and `HK_ENABLE_APP_*` composition, and fails
when a committed generated composition file is missing or stale. Capability
composition consumes this same in-memory canonical model; it does not parse a
second app-requirements schema.

Composition discovers app ownership only from that recursively validated
canonical model. It treats `.c`, `.cc`, `.cpp`, and `.cxx` uniformly and rejects
every app-package production translation unit without a manifest owner. There
is no manual central app descriptor table: the small core registry only
iterates generated const arrays and dispatches through the typed entry binding.
For each enabled app, only its
app root and directories explicitly named by `private_includes` MAY become
private compiler include roots. Build tooling MUST remove other app header
directories promoted by recursive legacy-SDK discovery; an undeclared header
directory MUST NOT enter the compiler include search path.

## Native app manifest versus Phase 4 Project Format

The native app manifest describes C sources compiled into one firmware image.
It is not a developer project package, device filesystem object, installed
program, Python runtime selection, asset synchronization format, or executable
discovery record. It does not define multi-file project upload, atomic install
or update, rollback, startup project selection, dynamic loading, a Program
Manager, MicroPython API v2, or IDE workspace state.

Those concerns belong to the future Phase 4 Project Format and on-device
Program Manager. A future project manifest has an independent contract ID,
schema, version line, storage/lifecycle semantics, and compatibility checks; it
MUST NOT be treated as schema 1 of native `app.toml`.

## Compatibility

Schema major `1` is accepted only with Native App Manifest contract `0.1.x`, App
Runtime `0.1.x`, and Capability API `0.1.x`. Experimental consumers request
`[0.1.0, 0.2.0)`. Changing one version axis does not implicitly change the
others. A breaking schema or semantic change requires a new experimental MINOR
contract line, migration notes, and an ADR when architectural.

## References

- [App Runtime](APP_RUNTIME.md)
- [Feature App SDK](APP_SDK.md)
- [Capability API](CAPABILITY_API.md)
- [Versioning Policy](VERSIONING.md)
- [Architecture Vision](../ARCHITECTURE_VISION.md)
- [Roadmap](../ROADMAP.md)
- [ADR-0008](../adr/0008-generate-native-app-composition.md)
