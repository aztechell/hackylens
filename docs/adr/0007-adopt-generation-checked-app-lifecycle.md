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

The runtime binds descriptor-sized private state from fixed storage. Before
`probe`, it preflights every immutable manifest capability/service declaration
against the composed inventory without opening an owner or acquiring a lease.
Missing/incompatible required grants exclude the app; optional absence selects
only the named manifest fallback. Preflight availability is stable: if later
acquisition of an available optional fails, launch fails and owner-wide unwind
runs instead of changing the status observed by `probe`. After a successful
`probe`, runtime opens one
generation-checked owner and injects only those preflighted grants before
`prepare`. Undeclared access returns `HK_ERR_NOT_DECLARED` without reaching a
provider. `start` occurs only after successful injection and preparation.
Ordinary dispatch is synchronous and legal only while running.

The SDK context is a public fixed-capacity ABI object with immutable identity,
generation, one owner, and inline tables capped at 16 capability grants and 16
app-scoped service handles. It has no runtime, provider, driver, HAL, board, or
platform pointer and reuses public Capability API handle types directly.
Manifest validation and runtime binding both reject count overflow.
Lifecycle callbacks receive a const context and obtain writable app state
through the state accessor.

The context owner and grant tables are read-only snapshots. Runtime retains the
authoritative owner and preflight availability in private instance state;
acquisition, owner cleanup, and invalidation never trust fields read back from
the callback-visible context. Context corruption therefore cannot suppress
owner-wide cleanup.

Teardown is one irreversible sequence: idempotent stop, app cleanup at most once
while context and handles remain valid, mandatory runtime owner-wide cleanup,
provider quarantine after failed provider cleanup, handle and deferred-token
invalidation, context-generation invalidation, and only then state reuse. A
failure at one unwind step is retained but cannot skip a later step.
Partial injection and owner-table exhaustion follow the same deterministic
unwind rules. Handles remain usable through app cleanup, are invalidated after
owner-wide cleanup, and copied old-generation contexts/handles never regain
authority when state is reused.

The runtime creates one finite absolute monotonic teardown deadline when
teardown begins. Its finite positive budget comes from runtime policy, not the
app manifest, and its time origin comes through the existing public Time
Capability provider and one `hk_time_deadline_after_us` call with a private
runtime owner rather than a raw or parallel clock. `stop`, app cleanup, and
owner-wide cleanup share that stored deadline; it is never refreshed per stage,
provider, retry, or affinity handoff. The borrowed context exposes the same
value through the SDK so app cleanup can pass it to public release operations.
Expiration or app-cleanup failure still leads to owner-wide cleanup with the
same already-expired deadline, provider quarantine where Phase 2 cleanup cannot
reach safety, and deterministic invalidation/state retirement.

Deferred provider work uses a bounded runtime-owned slot/generation/epoch token.
An old completion is discarded before app code or state access. Generation
values retire on exhaustion instead of wrapping into a live instance.

Firmware integration uses one fixed-capacity foreground switch for v2 and
legacy descriptors. Menu selection, BACK, autostart, debug-forced menu,
safe-mode suppression, rapid replacement, and callback failure all enter the
same close/open algorithm. Legacy enter/exit remains behavior-compatible behind
that boundary and uses the same capability-owner cleanup. The runtime consumes
events from the existing Input and SD paths; it adds no sampler or queue.

A running callback failure retains its original diagnostic, delivers one
Runtime Close event with the callback-failed reason, and then follows the same
stop/app-cleanup/owner-cleanup path. A Runtime Close callback failure cannot
replace that original cause. An invalidation requested during `render` remains
pending and forces an immediate next poll instead of waiting for a later tick.

The public event ABI is one bounded ordered union for Input, SD/media, Timer,
Runtime Close, and generation-checked Wakeup. BACK is consumed as navigation.
Ticks use manifest interval/budget and the composed monotonic Time provider,
with no catch-up loop. Rendering uses at most eight invalidations and a
callback-borrowed opaque surface over the injected Display handle; runtime alone
owns batch begin/present/abort and no public `screen_t`, LCD owner, or raw
framebuffer is introduced.

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
The integration adds one fixed switch object and no heap, task, general queue,
runtime core, second input path, or framebuffer.

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
- Mixed legacy/v2 host tests cover ordered events, render invalidation, rapid
  switch, BACK during start, deadline excess, render failure, autostart fallback,
  and stale wakeup rejection.

## References

- [App Runtime](../spec/APP_RUNTIME.md)
- [Feature App SDK](../spec/APP_SDK.md)
- [Capability API](../spec/CAPABILITY_API.md)
- [Legacy App Lifecycle](../APP_LIFECYCLE.md)
- [ADR-0005](0005-adopt-owner-scoped-capability-handles.md)
- [Phase 3 Masterplan](../PHASE3_MASTERPLAN.md)
