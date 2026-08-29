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

Every non-zero uint16 autostart ID is emitted into an immutable reserved-ID set
before app enable flags are applied. Persisted validity is exact set membership,
not a dense numeric range; disabled apps retain their reserved identity.
Autostart choices enumerate enabled eligible canonical descriptors without
using menu visibility or menu order. Settings schema v5 stores the complete
uint16 value and migrates schema-v4 uint8 IDs by zero extension.

Legacy apps name an explicit app-owned const `hk_legacy_app_entry_t` binding
object in their manifests until migration. Generated descriptors reference
that typed object; callback symbols are not guessed or repeated in a central C
table. Lifecycle-v2 entries use the separate typed descriptor branch. Apps do
not retain a separate production registry or requirements table. Generated
policy uses layer-generic rules and contains no per-app architecture allowlist.

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
in package 3.1. Package 3.3 replaced the requirements schema and manual app
source/enable lists after validator and disabled-build parity. Package 3.4
replaced the manual registry with immutable generated descriptors, app-owned
legacy bindings, and a small generic registry runtime after parity tests.
Project Format remains unpublished and independent.

## Evidence

- Native App Manifest contract fixes build-time-only authority and Phase 4
  separation.
- Documentation guards reject forbidden Phase 4 fields and incompatible
  Phase 3 contract metadata.
- Architecture policy names manifest and generated-registry layers without an
  app-specific allowlist.
- Package 3.3 checks generator freshness and deterministic order and proves
  disabled source/third-party isolation.
- Package 3.4 checks empty, single, all-enabled, disabled, and mixed legacy/v2
  generation; host runtime tests preserve menu, autostart, screen, SD, debug,
  tick, capability, and service dispatch while the architecture guard rejects
  manual registry reintroduction and board/HAL/driver policy in generated code.
- Package 3.4 correction tests IDs above 255 across settings save/load, exact
  sparse-ID membership, hidden eligible enumeration, disabled-ID reservation,
  and schema-v4 settings migration without changing the 124-byte record size.

## References

- [Native App Manifest](../spec/APP_MANIFEST.md)
- [App Runtime](../spec/APP_RUNTIME.md)
- [Feature App SDK](../spec/APP_SDK.md)
- [Versioning Policy](../spec/VERSIONING.md)
- [ADR-0006](0006-generate-immutable-capability-inventory.md)
- [Phase 3 Masterplan](../PHASE3_MASTERPLAN.md)
