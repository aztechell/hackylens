---
adr: 0008
title: Generate native app composition from build-time manifests
status: accepted
date: 2026-08-26
deciders: platform-architecture
supersedes:
superseded-by:
---

# ADR-0008: Generate native app composition from build-time manifests

## Context

Phase 2 composition has a manual app source list, a manual `app_registry.c`, and
a private `app_requirements.toml`. Those sources preserve current behavior but
duplicate identity, capability grants, source inclusion, menu order, autostart,
help, debug, and test facts. Adding a portable app therefore still requires
editing platform-core build and registry files.

Phase 3 needs one public native app manifest without introducing the separate
Phase 4 Project Format, on-device installation, dynamic loading, or a runtime
TOML dependency.

## Decision

Adopt Native App Manifest `0.1.0 experimental`, schema major `1`, as the sole
source of native app composition. Build tooling validates `app.toml` and emits a
canonical model, build source/include sets, enable definitions, capability and
service grants, immutable registry descriptors, menu/autostart/help/debug
metadata, resource reports, and test-matrix inputs.

Firmware receives const generated data only. There is no runtime parser,
filesystem discovery, mutable registration table, board-ID inference, or
dynamic code loader. Stable identity and ordering are explicit manifest values,
not directory, array, object, or link order.

Legacy apps name an explicit legacy entry adapter in their manifests until
migration. They do not retain a separate production registry or requirements
table. Generated policy uses layer-generic rules and contains no per-app
architecture allowlist.

## Alternatives

- Keep registry, build list, and requirements synchronized manually: rejected
  because divergence remains possible and adding an app changes core files.
- Parse TOML during boot: rejected because it adds flash/RAM/error surface and
  cannot change statically linked composition.
- Self-register app constructors at link or boot time: rejected because order,
  identity, exclusions, and capability validation become implicit.
- Reuse the Phase 4 Project Format: rejected because a compiled native app and
  an installable developer project have different identity, storage, runtime,
  update, and rollback contracts.
- Load native plugins dynamically: rejected as Phase 3 scope expansion without
  an MMU, measured budget, or need.

## Consequences

Build tooling gains strict schema/path/version validation and deterministic
generation. Generated artifacts must be freshness-checked, and a malformed or
incompatible app fails before compile. Firmware becomes simpler because it
iterates immutable descriptors and contains no parser or discovery path.

The migration temporarily keeps legacy callback implementations, but not a
second source of composition truth. Later packages must prove disabled apps
leave no private objects or resource sections.

## Compatibility and Migration

This adds `hackylens.native-app-manifest` `0.1.0 experimental`, schema major `1`,
compatible with App Runtime, Feature App SDK, and Capability API `0.1.x` through
exclusive upper bound `0.2.0`.

The private Phase 2 requirements schema and manual registry remained unchanged
in package 3.1. Package 3.3 replaces the requirements schema and manual app
source/enable lists after validator and disabled-build parity; the manual
registry remains until generated-descriptor/legacy-adapter parity in 3.4.
Project Format remains unpublished and independent.

## Evidence

- Native App Manifest contract fixes build-time-only authority and Phase 4
  separation.
- Documentation guards reject forbidden Phase 4 fields and incompatible
  Phase 3 contract metadata.
- Architecture policy names manifest and generated-registry layers without an
  app-specific allowlist.
- Package 3.3 checks generator freshness and deterministic order and proves
  disabled source/third-party isolation. Registry parity remains the 3.4 gate
  before the manual registry is removed.

## References

- [Native App Manifest](../spec/APP_MANIFEST.md)
- [App Runtime](../spec/APP_RUNTIME.md)
- [Feature App SDK](../spec/APP_SDK.md)
- [Versioning Policy](../spec/VERSIONING.md)
- [ADR-0006](0006-generate-immutable-capability-inventory.md)
- [Phase 3 Masterplan](../PHASE3_MASTERPLAN.md)
