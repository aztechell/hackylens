---
contract-id: hackylens.app-runtime
owner: firmware-runtime
version: 0.2.0
stability: experimental
compatibility-app-manifest: >=0.1.0,<0.2.0
compatibility-capability-api: >=0.1.0,<0.2.0
---

# HackyLens App Runtime

This contract describes the production runtime in `firmware/src/runtime/`.
Update it with changes to the public SDK, implementation and behavioral tests;
see the [change process](README.md).

## Typed-service cleanup

Time and Input bindings have board lifetime. Lights and Display sessions obtained
through the app context reside in private runtime storage and are retired with
the original teardown deadline, even if stop or earlier cleanup fails. Camera
session retirement is an explicit production fallback. Neither path retires
persistent settings or MicroPython worker sessions. Those MP sessions are
retired only at the worker's terminal handoff.

Display uses `hk_app_context_display(ctx, plane, &session)` and stable runtime
storage. UI borrows that session by pointer; it never reconstructs a handle per
frame. BASE and OVERLAY ownership and real frame/transaction generations remain.
External Link now uses a runtime-owned connector session via
`hk_app_context_external_link(ctx, mode_features, &session)`. Its retirement
shares the original teardown deadline with Lights and Display. Service retirement must complete its logical
invalidation before app storage is reused; a failed safe-off quarantines the
affected resource. No generic broker owner or grant cleanup remains.

## Purpose and scope

This contract defines the lifecycle, ownership, failure unwind, and stale-work
rules for statically linked native feature apps. Version `0.2.0` supports one
foreground lifecycle-v2 app at a time. Dispatch is synchronous, bounded, and
allocation-free.

The runtime consumes immutable descriptors generated at build time. It does not
parse TOML, discover apps on a filesystem, register apps during boot, load
native code dynamically, or provide a Project Format or Program
Manager. All twelve bundled apps use the same typed entry and foreground
switch. No legacy descriptor, selector, or adapter remains.

## Public lifecycle

The lifecycle has `start`, `event`, optional `render`, and `stop`:

```text
validate immutable descriptor
-> initialize context and production service scope
-> start(ctx)
-> event(ctx, event) / render(ctx, surface)
-> stop(ctx)
```

`start` absorbs the former probe/prepare work and initializes app state after
runtime has established the context and production service scope. There is no public
`probe`, `prepare`, `tick`, or app `cleanup` callback. Timer due times arrive as
`HK_APP_EVENT_TIMER`. App-owned resource release belongs in `stop`; runtime
scoped service cleanup always follows `stop`.

`render` remains a separate callback because the runtime, not the app, owns the
Display transaction: begin, apply dirty regions, present or abort. PONG dirty
regions and BUTTONS chrome both use that guarantee. Menu `draw_icon` is a
descriptor/menu presentation hook, not a lifecycle callback.

All callbacks execute on the runtime dispatch context and MUST be synchronous,
bounded, non-blocking except for operations bounded by an injected capability
deadline, and add no runtime allocation. Existing decoder/model services retain their
resource ownership and bounded cleanup. A callback MUST NOT create a task, queue,
core, hidden loop, or unbounded retry. `HK_PENDING` is not a valid lifecycle
callback result. Every lifecycle callback receives
`const hk_app_context_t *`; writable app state is obtained only through the
bounded state accessor. `stop` takes only `ctx`; the one teardown deadline is
read through `hk_app_context_teardown_deadline`. Internal stop reasons stay in
runtime bookkeeping and the Runtime Close event; they are not stop-callback
arguments.

Required-service availability is checked at build time. Runtime performs no
inventory discovery, version negotiation or generic grant injection. Time and
Input use immutable bindings. Lights, Display and External Link accessors open
stable sessions in private runtime storage; providers enforce actual conflicts.
Production integration opens and binds BASE before `start`, including apps
that draw through existing portable UI services during startup. If preparation
fails after opening a resource, common teardown retires that partial scope.
`start` must not leave an effect that `stop` plus scoped retirement cannot release.

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
the original callback diagnostic and cannot skip stop or scoped service cleanup.
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
the next due time is `callback_started_now + tick_interval_us`. Clock failure,
backward time, arithmetic overflow, or elapsed budget excess terminates the app
with `HK_APP_STOP_DEADLINE`; the deadline is not refreshed into retries. Cadence and callback budget are
independent: QR may legitimately spend more than one period decoding a frame.

## Render and invalidation

`hk_app_context_request_render` records either a full invalidation or at most
eight fixed-capacity dirty rectangles. A successful start with a non-null
`render` begins with one full invalidation. A null `render` means the app uses
an existing firmware presentation service; runtime opens no competing batch
and render requests return `HK_ERR_FEATURE_UNAVAILABLE`. The app cannot present, begin/abort a Display batch, select
an LCD plane, obtain a framebuffer owner, or call a driver through this API.

For a render pass the runtime borrows its stable public Display session,
opens one bounded provider transaction, applies the pending invalidations, and
passes an opaque `hk_app_surface_t` to `render`. Command-batch drawing
(clear/rectangle/text/blit) and `hk_app_surface_lock` of the existing BASE
backing store are mutually exclusive for that pass; lock does not allocate a
second framebuffer. The surface is valid only during that render callback and
is invalidated before any later app work. Runtime alone presents or aborts the
transaction with one absolute deadline derived
from `limits.render_budget_us`; measured callback time uses the same monotonic
Time provider. A callback, provider, present, or budget failure aborts the
batch where possible and enters the common unwind. Production integration opens
BASE before app start; absent Display fails that launch. Required Display
availability is also checked by build composition. A busy
Display keeps the invalidation pending for a later poll.

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
sessions but MUST NOT invalidate the context or skip runtime retirement.

## State machine

The public instance states are `INACTIVE`, `STARTING`, `RUNNING`, `STOPPING`,
and `FAULTED`. The private launch/teardown stages are:

```text
REUSABLE
  -> STARTING
  -> RUNNING
  -> STOPPING
  -> SCOPE_CLEANUP
  -> INVALIDATING
  -> REUSABLE
```

`STARTING` covers scope preparation and the `start` callback. `STOPPING` is the
`stop` callback. `SCOPE_CLEANUP` retires runtime-owned service sessions and
production fallback resources. Generation exhaustion leaves the slot faulted
until reboot; generations do not wrap.

The app entry binds its fixed-capacity state storage and size. The descriptor
uses the fixed Feature App ABI alignment. Before `STARTING`, runtime checks its capacity and
address against those immutable descriptor values, clears the declared byte
range, and does not return that storage to `REUSABLE` until invalidation is
complete. App state is never allocated from a heap and is not shared between
generations.

Descriptor identity, typed entry, finite limits,
menu/autostart metadata, and help/debug text are immutable generated
data. The runtime may retain a descriptor pointer for one instance but MUST NOT
modify it, construct a replacement, register another descriptor at boot, or
derive identity from registry/menu position.

Only `RUNNING` accepts ordinary dispatch. Once teardown is requested, no new
`event` or `render` callback may begin. A callback already on the synchronous
call stack completes or reaches its bounded terminal failure before the state
advances to `STOPPING`.

## Failure unwind

Every terminal failure follows the deepest lifecycle stage reached:

| Failure point | App `stop` | Scoped cleanup |
|---|---:|---:|
| descriptor/state validation before preparation | no | no live scope |
| partial production scope preparation | no | yes |
| `start` entered, including failure | exactly once | yes |
| running callback, close request or exit | exactly once | yes |

A callback error is retained for diagnostics but does not skip a later unwind
stage. An error from `stop`, or an already-expired teardown deadline, does not
skip runtime scoped service cleanup. Provider quarantine stays local to the failed
provider and MUST NOT move the whole runtime into `FAULTED`. The runtime
invokes `stop` at most once, while the context and its app-scoped handles are
still valid.

## Teardown deadline

When teardown is accepted and before entering `STOPPING`, the runtime creates
exactly one finite absolute monotonic teardown deadline. It obtains monotonic
time through the same statically bound Time service used by the
firmware; App Runtime MUST NOT read a raw platform clock or introduce another
time implementation. Using the immutable Time binding, it calls public
`hk_time_deadline_after_us` exactly once with the finite
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

That one deadline covers `stop(ctx)` and runtime scoped service
cleanup together. It MUST NOT be refreshed between stages, sessions, services,
providers, retries, affinity dispatches, or cleanup calls. The runtime passes
the same stored absolute value to every close and retire operation even
after it expires.

If `stop` consumes or exceeds the deadline, runtime scoped service cleanup MUST
still be attempted with the same already-expired absolute deadline. A provider
that cannot reach its bounded safe state performs logical retirement and
quarantines the affected resource. Timeout or failure cannot skip handle and token
invalidation, context invalidation, deterministic state clearing, or slot
retirement/reuse according to the generation rules.

## Normative teardown order

Teardown order is fixed and MUST NOT be reordered:

1. call idempotent `stop(ctx)` when `start` was entered;
2. perform bounded runtime scoped service cleanup with the
   same stored deadline;
3. quarantine every provider whose retirement cannot reach a safe state,
   following the Capability API rules;
4. invalidate all app-scoped handles and deferred-work tokens;
5. invalidate the context generation;
6. make the cleared app state slot reusable.

Provider cleanup is non-cancellable and receives the single stored teardown
deadline defined above. Failed provider cleanup invalidates its sessions and
quarantines the affected resource before another session can open it. State reuse MUST
NOT occur early merely because `stop` returned an error or exceeded the
deadline.

## Context and handle ownership

`hk_app_context_t` is runtime-owned. An app receives a borrowed reference valid
only for the current callback and instance generation. It MUST NOT copy the
context for later use, mutate runtime fields, or derive provider, service,
driver, HAL, route, peripheral, board, or platform objects from it.

The public context carries ABI size/version, immutable app identity, context
generation and Time/Input binding pointers. Runtime owns the stable session
storage privately. Neither copying the callback context nor modifying its
snapshot can suppress cleanup or authorize a later instance.

Typed accessors return existing public service types without parallel SDK
wrappers. Sessions remain usable during `stop`; new opens are rejected during
teardown. Runtime retires all sessions before invalidating context and reusing
state. Time/Input bindings themselves have board lifetime. Session-address
identity and asynchronous operation generations protect the lifetimes that
still exist; there are no generic owner/grant/lease tables.

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
capacities accounted for in [RAM / Flash Budget](../RAM_BUDGET.md). The runtime
adds no heap allocation, task, queue, core, or full framebuffer. Measure lifecycle
dispatch overhead separately from callback body and provider I/O.

## Compatibility

App Runtime `0.2.x` accepts native App Manifest `0.1.x` with schema major `1`
and Capability API `0.1.x`. Experimental Feature App SDK consumers request
runtime `[0.2.0, 0.3.0)`. Because the contract is experimental, a future `0.3.0`
line may be breaking. Firmware, HMPY, Board Port, and
MicroPython API versions do not change merely because this contract is
published. The eight-callback `0.1.x` lifecycle is not retained behind a
compatibility wrapper. There is no legacy adapter or manifest lifecycle selector. Existing bundled camera/media services are not standalone SDK APIs.

## References

- [Native App Manifest](APP_MANIFEST.md)
- [Feature App SDK](APP_SDK.md)
- [Capability API](CAPABILITY_API.md)
- [Versioning Policy](VERSIONING.md)
