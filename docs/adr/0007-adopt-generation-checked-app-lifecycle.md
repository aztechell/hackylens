---
adr: 0007
title: Adopt generation-checked native app lifecycle
status: accepted
date: 2026-08-26
deciders: platform-architecture
supersedes:
superseded-by:
---

# ADR-0007: Adopt generation-checked native app lifecycle

## Context

The legacy `hk_app_t` lifecycle is screen-oriented and distributes cleanup
across app callbacks, background ticks, runtime owner wiring, and provider
behavior. Phase 2 added generation-checked capability owners and bounded
owner-wide cleanup without changing that ABI. Phase 3 now needs a public native
app lifecycle that can inject only declared handles, unwind every partial start,
and prevent deferred work from touching reused state.

The firmware must remain fixed-capacity and cooperative. There may be only one
foreground lifecycle-v2 app, and the decision cannot add heap-backed instances,
tasks, queues, cores, or asynchronous app callbacks.

## Decision

Adopt App Runtime `0.1.0 experimental` with the ordered callbacks
`probe/prepare/start/event/tick/render/stop/cleanup` and a runtime-owned,
generation-checked context.

The runtime binds descriptor-sized private state from fixed storage. `probe`
retains no resources; declared handles are injected before `prepare`; `start`
occurs only after successful injection and preparation. Ordinary dispatch is
synchronous and legal only while running.

Teardown is one irreversible sequence: idempotent stop, app cleanup at most once
while context and handles remain valid, mandatory runtime owner-wide cleanup,
provider quarantine after failed provider cleanup, handle and deferred-token
invalidation, context-generation invalidation, and only then state reuse. A
failure at one unwind step is retained but cannot skip a later step.

Deferred provider work uses a bounded runtime-owned slot/generation/epoch token.
An old completion is discarded before app code or state access. Generation
values retire on exhaustion instead of wrapping into a live instance.

## Alternatives

- Extend `hk_app_t` in place: rejected because screen identity, legacy callbacks,
  and partial cleanup would become the public v2 ABI.
- Let every app own acquisition and teardown: rejected because failed app
  cleanup could bypass Phase 2 owner cleanup and quarantine.
- Allocate app objects and futures on a heap: rejected because memory and stale
  work would no longer be statically bounded.
- Run app callbacks on a new task or general queue: rejected because it changes
  core/affinity behavior and introduces unbounded scheduling state.
- Invalidate context before app cleanup: rejected because cleanup must use its
  valid declared handles to release app-local effects.

## Consequences

Lifecycle and cleanup become deterministic and testable at every transition.
Apps cannot treat screen values as identity or retain a context across
generations. Runtime implementation needs fixed instance, token, and transition
state whose flash, static RAM, stack, and dispatch cost must fit the Phase 3
baseline budgets.

The legacy registry can coexist only behind an explicit adapter. It receives no
exception to manifest composition, owner cleanup, or architecture policy.

## Compatibility and Migration

This adds `hackylens.app-runtime` `0.1.0 experimental` and the matching Feature
App SDK/Native App Manifest compatibility line `[0.1.0, 0.2.0)`. It does not
modify `hackylens.legacy-app-lifecycle` `0.2.0`; BUTTONS, PONG, and CAMERA migrate
in later Phase 3 packages.

Capability API handles and Phase 2 cleanup/quarantine semantics are reused
unchanged. Firmware, HMPY, Board Port, and MicroPython API versions do not change.

## Evidence

- App Runtime contract state, unwind, stop-reason, and stale-work tables.
- Documentation negative tests for incompatible Phase 3 contract versions.
- Declarative architecture policy for SDK and runtime-private directions.
- Phase 3 baseline pins the exact Phase 2 closure before runtime source changes.
- Runtime transition/fault-injection tests and measured dispatch evidence are
  required by later implementation packages.

## References

- [App Runtime](../spec/APP_RUNTIME.md)
- [Feature App SDK](../spec/APP_SDK.md)
- [Capability API](../spec/CAPABILITY_API.md)
- [Legacy App Lifecycle](../APP_LIFECYCLE.md)
- [ADR-0005](0005-adopt-owner-scoped-capability-handles.md)
- [Phase 3 Masterplan](../PHASE3_MASTERPLAN.md)
