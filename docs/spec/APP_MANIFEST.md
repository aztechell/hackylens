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
are errors. Detailed schema-1 field grammar and canonicalization are introduced
by Phase 3.2 without weakening the constraints in this document.

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
