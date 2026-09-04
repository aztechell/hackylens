---
contract-id: hackylens.app-runtime
owner: firmware-runtime
version: 0.2.0
stability: experimental
phase: 3
compatibility-app-manifest: >=0.1.0,<0.2.0
compatibility-capability-api: >=0.1.0,<0.2.0
---

# HackyLens App Runtime

## Purpose and scope

This contract defines the lifecycle, ownership, failure unwind, and stale-work
rules for statically linked native feature apps. Version `0.2.0` supports one
foreground lifecycle-v2 app at a time. Dispatch is synchronous, bounded, and
allocation-free.

The runtime consumes immutable descriptors generated at build time. It does not
parse TOML, discover apps on a filesystem, register apps during boot, load
native code dynamically, or provide the Phase 4 Project Format and Program
Manager. The existing [legacy lifecycle](../APP_LIFECYCLE.md) remains a separate
compatibility surface until each app is migrated through an explicit adapter.
For a legacy manifest, its `entry` symbol names one app-owned immutable
`hk_legacy_app_entry_t` binding object. The generated descriptor stores a typed
pointer to that object; generic registry code does not copy callback symbols or
select concrete apps. A lifecycle-v2 manifest instead produces the typed v2
descriptor branch consumed by the runtime defined here. The legacy binding is
private firmware compatibility machinery, not an SDK ABI.

## Public lifecycle

The public lifecycle is four callbacks:

```text
validate immutable descriptor
-> resolve declared capability/service availability
-> capability/service injection
-> start(ctx)
-> event(ctx, event) / render(ctx, surface)
-> stop(ctx)
```

`start` absorbs the former probe/prepare work: identity, declared availability,
injected handles, and app-state initialization all happen there after the
runtime has created the owner and injected exact grants. There is no public
`probe`, `prepare`, `tick`, or app `cleanup` callback. Timer due times arrive as
`HK_APP_EVENT_TIMER`. App-owned resource release belongs in `stop`; runtime
owner-wide capability/service cleanup always follows `stop`.

`render` remains a separate callback because the runtime, not the app, owns the
Display transaction: begin, apply dirty regions, present or abort. PONG dirty
regions and BUTTONS chrome both use that guarantee. Menu `draw_icon` is a
descriptor/menu presentation hook, not a lifecycle callback.

All callbacks execute on the runtime dispatch context and MUST be synchronous,
bounded, non-blocking except for operations bounded by an injected capability
deadline, and free of heap allocation. A callback MUST NOT create a task, queue,
core, hidden loop, or unbounded retry. `HK_PENDING` is not a valid lifecycle
callback result. Every lifecycle callback receives
`const hk_app_context_t *`; writable app state is obtained only through the
bounded state accessor. `stop` takes only `ctx`; the one teardown deadline is
read through `hk_app_context_teardown_deadline`. Internal stop reasons stay in
runtime bookkeeping and the Runtime Close event; they are not stop-callback
arguments.

Before `start`, the runtime resolves every generated manifest declaration
against the composed inventory. This is a resource-free preflight: it records
the exact capability ID/instance request and whether an optional declaration
uses its named fallback, but creates neither an app owner nor a lease. A missing
required capability, incompatible version, unavailable required feature, or
unresolved service excludes the app before any lifecycle callback. An optional
capability is either recorded available or recorded absent with exactly its
manifest fallback; there is no hardware-derived fallback. This availability is
stable for the launch. If acquisition of a capability recorded available later
fails for any reason, launch fails, `start` is not called, and runtime performs
owner-wide unwind. Runtime MUST NOT silently change it to fallback after
`start` has observed it.

After successful preflight the runtime creates the one app owner and acquires
the preflighted grants. Required and available optional handles plus app-scoped
service handles are injected before `start`. No callback can request an
undeclared grant; `HK_ERR_NOT_DECLARED` is returned before provider access.
`start` may initialize app state and use injected handles; it MUST NOT leave an
effect that `stop` plus owner-wide cleanup cannot release.

`event` and `render` are legal only in `RUNNING`. `hk_app_context_request_render`
and `hk_app_context_request_close` are also legal only in `RUNNING` during a
callback. The runtime supplies ordered events, monotonic time, and a bounded
rendering surface through public SDK contracts. The app does not poll a raw
button sampler, select a hardware clock, or own an LCD driver path.

## Event model

`hk_app_event_t` is a size/versioned fixed union. Version 1 has exactly five
kinds: Input, SD/media change, timer, runtime close, and app-private wakeup.
There is no heap-backed event object and no App Runtime queue. Dispatch is
synchronous on the existing firmware loop. Every accepted event in one app
generation has a non-zero, strictly increasing runtime sequence; the sequence
restarts only for a newly launched generation.

Input events copy the existing `hk_input_event_t` produced by the one composed
Input Capability provider, including provider sequence, monotonic timestamp,
state transitions, and overflow `dropped` count. The firmware MUST NOT sample a
second button path for v2 apps. BACK is delivered as an ordinary Input event to
the active v2 app. The switch MUST NOT intercept BACK to close the app. An app
that should leave on BACK calls `hk_app_context_request_close` from its event
callback; BUTTONS instead treats BACK as a testable button. A BACK Input
received reentrantly while `start` is executing is retained as one pending
input and dispatched after successful start; `hk_app_switch_close` during
`start` remains a pending close (`HK_PENDING`) and still unwinds that instance
before it becomes independently dispatchable.

SD/media events contain an insertion/removal/mounted/error kind and a monotonic
media generation. They are adapted from the existing firmware SD event path;
they do not expose raw SD blocks, filesystem internals, or platform paths.

A runtime-close event is delivered exactly once immediately before a normal
running-instance teardown enters `STOPPING`. It contains the retained stop
reason and is ordered after all previously accepted events. Its failure is
retained but cannot cancel or skip teardown. Start failure has not reached the
running event surface and therefore proceeds directly through the documented
failure unwind.

A terminal `event` or `render` callback failure uses that same running instance
termination path. Runtime first retains the original callback error, then
delivers exactly one Runtime Close event with `HK_APP_STOP_CALLBACK_FAILED`, and
only then enters teardown. Failure of this Runtime Close callback cannot replace
the original callback diagnostic and cannot skip stop or owner-wide cleanup.
`hk_app_context_request_close` is legal only during a `RUNNING` callback; after
that callback returns `HK_OK`, runtime terminates with
`HK_APP_STOP_COMPLETED`.

The wakeup payload contains only a fixed-size token with runtime slot, context
generation, instance epoch, and app-private value. The runtime validates all
three authority fields before dispatch. A stale completion is rejected before
calling app code; it cannot render, write state, or clean up a later app.

## Tick scheduling

After successful `start`, the runtime reads monotonic time through the same
composed public Time Capability and sets the first due time to
`now + limits.tick_interval_us`. When due, it dispatches one ordered Timer event
with scheduled and observed monotonic times. There is no separate `tick`
callback. It measures that Timer-event callback against
`limits.tick_budget_us`. There is no catch-up loop: after successful completion
the next due time is `completion_now + tick_interval_us`. Clock failure,
backward time, arithmetic overflow, or elapsed budget excess terminates the app
with `HK_APP_STOP_DEADLINE`; the deadline is not refreshed into retries.

## Render and invalidation

`hk_app_context_request_render` records either a full invalidation or at most
eight fixed-capacity dirty rectangles. A successful v2 start begins with one
full invalidation. The app cannot present, begin/abort a Display batch, select
an LCD plane, obtain a framebuffer owner, or call a driver through this API.

For a render pass the runtime borrows the app's injected public Display handle,
opens one bounded provider transaction, applies the pending invalidations, and
passes an opaque `hk_app_surface_t` to `render`. Command-batch drawing
(clear/rectangle/text/blit) and `hk_app_surface_lock` of the existing BASE
backing store are mutually exclusive for that pass; lock does not allocate a
second framebuffer. The surface is valid only during that render callback and
is invalidated before any later app work. Runtime alone presents or aborts the
transaction with one absolute deadline derived
from `limits.render_budget_us`; measured callback time uses the same monotonic
Time provider. A callback, provider, present, or budget failure aborts the
batch where possible and enters the common unwind. If an optional Display grant
is absent, the invalidation is deterministically consumed without opening a
hardware path; a required absent Display has already failed preflight.

If `render` requests another invalidation after runtime has consumed the
current pending set, that invalidation remains pending for a new render pass.
The foreground switch MUST schedule the next poll immediately and MUST NOT
delay that pass until the next manifest tick interval.

## Foreground switching

There is one fixed-capacity foreground switch state and one transition
algorithm for menu selection, app-requested close, autostart, debug-forced menu,
safe-mode autostart suppression, and callback failure. Opening another app first
closes the active app through the common boundary. An explicit switch close
received reentrantly during `start` is retained, launch finishes its bounded
callback, and the same instance is immediately unwound; it never becomes an
independently dispatchable foreground app. Autostart open failure leaves no
active instance and the existing controller falls back to MENU through that same
close/open boundary. Portable v2 apps MUST NOT call menu or screen runtime
directly; they request close through `hk_app_context_request_close`.

Legacy descriptors use a private adapter at this switch boundary. The adapter
preserves the existing input snapshot, screen, enter/exit, tick, SD, debug, and
secondary-screen behavior, while owner creation and owner-wide cleanup use the
same production capability-owner runtime. It is not a public SDK alternative
and it does not bypass manifest composition.

## Stop reasons

The first teardown cause is retained as the stop reason for the instance:

| Value | Name | Meaning |
|---:|---|---|
| `0` | `HK_APP_STOP_COMPLETED` | App requested normal completion |
| `1` | `HK_APP_STOP_BACK` | User BACK navigation |
| `2` | `HK_APP_STOP_SWITCH` | Foreground app switch or menu selection |
| `3` | `HK_APP_STOP_START_FAILED` | `start` returned a terminal failure |
| `4` | `HK_APP_STOP_CALLBACK_FAILED` | `event` or `render` failed |
| `5` | `HK_APP_STOP_DEADLINE` | A lifecycle budget or deadline was exceeded |
| `6` | `HK_APP_STOP_FORCED` | Debug, recovery, or fault policy forced teardown |
| `7` | `HK_APP_STOP_SHUTDOWN` | Runtime or device shutdown |

Unknown values MUST be handled as `HK_APP_STOP_FORCED`. A later request cannot
replace the first retained cause. These reasons remain internal runtime and
Runtime Close diagnostics; `stop(ctx)` does not receive them.
`stop` is required to be idempotent even though the runtime invokes it at most
once for one teardown. It may quiesce app logic and release app-owned extra
leases but MUST NOT invalidate the context or perform owner-wide cleanup.

## State machine

The public instance states are `INACTIVE`, `STARTING`, `RUNNING`, `STOPPING`,
and `FAULTED`. The private launch/teardown stages are:

```text
REUSABLE
  -> STARTING
  -> RUNNING
  -> STOPPING
  -> OWNER_CLEANUP
  -> INVALIDATING
  -> REUSABLE
```

`STARTING` covers owner creation, grant injection, and the `start` callback.
`STOPPING` is the `stop` callback. `OWNER_CLEANUP` is runtime owner-wide
cleanup. Generation exhaustion leaves the slot `FAULTED`/`INVALIDATING` until
reboot. There is no `PROBING`, `PREPARING`, or `APP_CLEANUP` stage.

The generated descriptor declares the manifest-derived private state size and
the fixed Feature App ABI alignment. A lifecycle-v2 entry binds app-owned
fixed-capacity storage. Before `STARTING`, runtime checks its capacity and
address against those immutable descriptor values, clears the declared byte
range, and does not return that storage to `REUSABLE` until invalidation is
complete. App state is never allocated from a heap and is not shared between
generations.

Descriptor identity, lifecycle kind, finite limits, capability/service
requests, menu/autostart metadata, and help/debug text are immutable generated
data. The runtime may retain a descriptor pointer for one instance but MUST NOT
modify it, construct a replacement, register another descriptor at boot, or
derive identity from registry/menu position.

The generated capability and service counts must fit the public context's fixed
capacities of 16 capability grants and 16 service handles. Both manifest
validation and runtime descriptor binding reject overflow; truncation and heap
growth are forbidden.

Only `RUNNING` accepts ordinary dispatch. Once teardown is requested, no new
`event` or `render` callback may begin. A callback already on the synchronous
call stack completes or reaches its bounded terminal failure before the state
advances to `STOPPING`.

## Failure unwind

Every terminal failure follows the deepest lifecycle stage reached:

| Failure point | App `stop` | Owner-wide cleanup |
|---|---:|---:|
| descriptor/state bind | no | no owner, otherwise yes |
| declared-surface preflight | no | no owner exists yet |
| owner open failed with a zero owner | no | no |
| injection after a live owner | no | yes |
| `start` entered, including `start` failure | exactly once | yes |
| running callback, close request, or exit | exactly once | yes |

A callback error is retained for diagnostics but does not skip a later unwind
stage. An error from `stop`, or an already-expired teardown deadline, does not
skip runtime owner-wide cleanup. Provider quarantine stays local to the failed
provider and MUST NOT move the whole runtime into `FAULTED`. The runtime
invokes `stop` at most once, while the context and its owner-scoped handles are
still valid.

## Teardown deadline

When teardown is accepted and before entering `STOPPING`, the runtime creates
exactly one finite absolute monotonic teardown deadline. It obtains monotonic
time through the same composed public Time Capability provider used by the
firmware; App Runtime MUST NOT read a raw platform clock or introduce another
time implementation. Using its private runtime owner and the one composed Time
handle, it calls public `hk_time_deadline_after_us` exactly once with the finite
positive `teardown_budget_us` policy value and stores the resulting
`hk_deadline_t` in the instance context. This runtime handle is not an app grant
and does not depend on whether the app declared Time. The policy is
runtime-controlled, build-time validated, and not configurable by `app.toml`,
an app callback, a provider, or a per-stage setting.

The public SDK exposes the stored value without reading time again:

```c
hk_result_t hk_app_context_teardown_deadline(
    const hk_app_context_t *ctx,
    hk_deadline_t *deadline);
```

The accessor returns the instance's exact stored deadline during `stop`;
outside teardown it returns `HK_ERR_INVALID_STATE`. It uses the
public Capability API `hk_deadline_t` directly and does not create an SDK time
type or a second clock path. If the initial Time Capability observation or
finite deadline calculation fails, teardown records that diagnostic, stores
`HK_DEADLINE_IMMEDIATE` as the single already-expired deadline, and continues
the full sequence.

That one deadline covers `stop(ctx)` and runtime owner-wide capability/service
cleanup together. It MUST NOT be refreshed between stages, leases, services,
providers, retries, affinity dispatches, or cleanup calls. The runtime passes
the same stored absolute value to every release and owner-close operation even
after it expires.

If `stop` consumes or exceeds the deadline, runtime owner-wide cleanup MUST
still be attempted with the same already-expired absolute deadline. A provider
that cannot reach its bounded safe state follows Phase 2 logical-close and
quarantine semantics. Timeout or failure cannot skip handle and token
invalidation, context invalidation, deterministic state clearing, or slot
retirement/reuse according to the generation rules.

## Normative teardown order

Teardown order is fixed and MUST NOT be reordered:

1. call idempotent `stop(ctx)` when `start` was entered;
2. perform bounded runtime owner-wide capability and service cleanup with the
   same stored deadline;
3. quarantine every provider whose owner cleanup cannot reach a safe state,
   following the Capability API rules;
4. invalidate all owner-scoped handles and deferred-work tokens;
5. invalidate the context generation;
6. make the cleared app state slot reusable.

Provider cleanup is non-cancellable and receives the single stored teardown
deadline defined above. Failed provider cleanup logically closes its leases and
quarantines the provider before any new owner can acquire it. State reuse MUST
NOT occur early merely because `stop` returned an error or exceeded the
deadline.

## Context and handle ownership

`hk_app_context_t` is runtime-owned. An app receives a borrowed reference valid
only for the current callback and instance generation. It MUST NOT copy the
context for later use, mutate runtime fields, or derive provider, service,
driver, HAL, route, peripheral, board, or platform objects from it.

The public context carries ABI size/version, immutable app identity, context
generation, the one owner after successful injection, and fixed-capacity inline
tables for exactly the generated capability and app-scoped service grants. It
contains no runtime, provider, service implementation, driver, HAL, board, or
platform pointer. From `start` through `stop`, acquired handles carry that same
owner and context generation. Runtime owner-wide cleanup follows `stop`; only
then are handles and the context invalidated.

The public owner, grant availability, leases, counts, and service handles are a
read-only callback snapshot. App Runtime keeps the authoritative owner and
preflight availability in private instance state. Acquisition, owner-wide
cleanup, and invalidation use only that private authority, never fields read
back from the public context. Consequently, even accidental corruption through
a cast cannot suppress owner-wide cleanup or change which grants runtime tries
to acquire.

Injected capability handles retain the public Capability API ABI. They are
owner-scoped and generation-checked; the App SDK does not wrap them in parallel
types without a documented ABI need. A handle not declared by the native app
manifest is not injected, and acquisition outside generated grants returns
`HK_ERR_NOT_DECLARED`.

If acquisition fails after any earlier grant was injected, the runtime skips
`start`, performs owner-wide cleanup for the same owner, invalidates every
partial handle and the context, and clears the state slot. Owner-table
exhaustion fails launch deterministically. A copied context or handle from an
old generation cannot become valid when the runtime slot is reused.

## Stale callbacks and deferred work

The runtime does not provide a general background queue. Deferred provider work
that already exists may complete only through a bounded runtime-owned token
containing slot, context generation, and instance epoch. The token is validated
before touching context or state and is delivered only as the ordered
app-private Wakeup event described above. Stop initiation retires the epoch;
handle invalidation retires remaining tokens.

A completion from an old epoch MUST be discarded without calling app code,
accessing the reused state slot, writing through an old buffer borrow, or using
new-generation handles. Generation exhaustion retires the slot until reboot; it
does not wrap silently.

## Memory and timing

Runtime tables, descriptors, tokens, state slots, and event storage have fixed
capacities accounted for in the Phase 3 resource report. The runtime adds no
heap allocation, task, queue, core, or full framebuffer. Lifecycle dispatch
overhead excludes callback body and provider I/O and is checked against the
[Phase 3 baseline](../PHASE3_BASELINE.md).

## Compatibility

App Runtime `0.2.x` accepts native App Manifest `0.1.x` with schema major `1`
and Capability API `0.1.x`. Experimental Feature App SDK consumers request
runtime `[0.2.0, 0.3.0)`. Because the contract is experimental, a future `0.3.0`
line may be breaking. Firmware, HMPY, Board Port, Legacy App Lifecycle, and
MicroPython API versions do not change merely because this contract is
published. The eight-callback `0.1.x` lifecycle is not retained behind a
compatibility wrapper; remaining production apps use one private legacy
adapter until they migrate.

## References

- [Native App Manifest](APP_MANIFEST.md)
- [Feature App SDK](APP_SDK.md)
- [Capability API](CAPABILITY_API.md)
- [Versioning Policy](VERSIONING.md)
- [Architecture Vision](../ARCHITECTURE_VISION.md)
- [Phase 3 Masterplan](../PHASE3_MASTERPLAN.md)
- [ADR-0007](../adr/0007-adopt-generation-checked-app-lifecycle.md)
