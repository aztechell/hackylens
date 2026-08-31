---
contract-id: hackylens.feature-app-sdk
owner: platform-architecture
version: 0.1.0
stability: experimental
phase: 3
compatibility-app-runtime: >=0.1.0,<0.2.0
compatibility-app-manifest: >=0.1.0,<0.2.0
compatibility-capability-api: >=0.1.0,<0.2.0
---

# HackyLens Feature App SDK

## Public entry surface

The Feature App SDK is the public C entry surface for lifecycle-v2 native apps.
All SDK-owned public headers live under `sdk/include`. An app includes only SDK
headers and the public capability types intentionally exposed or re-exported by
them.

The SDK MAY depend on public Capability API headers under
`firmware/include/hackylens/capability/`. That is the only permitted SDK to
Capability dependency direction. The SDK and apps MUST NOT include capability
implementation/provider/private headers, portable-service private headers,
storage internals, drivers, board/BSP headers, platform/HAL headers, the K210
SDK, runtime-private headers, or generated-registry private headers.

The SDK does not replace public capability types with parallel wrappers.
Types such as Time, Input, Display, and their owner-scoped handles retain the
Capability API ABI. A new wrapper type requires a concrete ABI, ownership, or
language-boundary reason recorded in the contract; naming convenience is not
sufficient.

## App-facing contracts

SDK `0.1.x` defines or re-exports only:

- lifecycle callback types and stop reasons from App Runtime `0.1.x`;
- the borrowed, generation-checked `hk_app_context_t` interface;
- ordered app event and monotonic tick values;
- the bounded view/surface interface backed by an injected Display handle;
- declared capability and app-scoped service handle access;
- fixed-capacity private app-state access;
- result, version, deadline, cancellation, buffer, and capability types from
  Capability API `0.1.x`.

The context exposes no mutable descriptor, registry table, provider vtable,
raw filesystem block access, unrestricted platform path, raw camera/DVP object,
peripheral instance, route, board ID inference, HAL object, or driver pointer.
Absence and optional fallback are explicit results, never guessed from hardware
identity.

The public definition is `sdk/include/hackylens/app/context.h`. It has ABI
size/version fields, immutable app identity, one generation-checked owner, and
inline fixed-capacity tables for at most 16 declared capability grants and 16
app-scoped service handles. It contains no runtime-private or hardware pointer.
The tables are runtime-owned observations, not mutable app registration or
discovery surfaces. Lifecycle callbacks receive the context through a
`const hk_app_context_t *`; writable app state remains available through
`hk_app_context_state` rather than by making the context mutable.

During `probe`, identity and declaration status accessors are valid while the
owner remains zero; typed handle access returns `HK_ERR_INVALID_STATE` because
acquisition has not occurred. From `prepare` through app `cleanup`, typed
accessors return the existing public Capability API handle for an available
declared `(id, instance)`. An absent optional returns
`HK_ERR_CAPABILITY_ABSENT` and its status accessor returns the exact manifest
fallback. An undeclared capability or service returns `HK_ERR_NOT_DECLARED`
without calling a provider. App-scoped service handles carry the same owner and
context generation. Availability observed by `probe` is stable: failure to
acquire a grant reported available fails launch and triggers owner-wide unwind;
it cannot become an implicit fallback before `prepare`.

The event, stop-reason, wakeup-token, and render-surface definitions are in
`sdk/include/hackylens/app/runtime.h`. `hk_app_event_t` is a bounded
size/versioned union for Input, SD/media, Timer, Runtime Close, and Wakeup. Its
runtime sequence is strictly increasing within one generation. Input embeds
the public Capability API `hk_input_event_t`; no parallel input or button type
is introduced. BACK remains runtime navigation and is not delivered as an
ordinary Input event to the closing app.

`hk_app_context_wakeup_token` creates a fixed slot/context-generation/epoch
token with one app-private value. Holding the token creates no work and grants
no authority by itself. A producer may return it only through a runtime-owned
bounded completion path; stale tokens are rejected before app dispatch.

`hk_app_context_request_render` records a full or rectangular invalidation.
The fixed limit is eight rectangles. `hk_app_surface_t` is opaque and borrowed
only for the current `render` callback. Its API permits display information,
invalidation, clear, rectangle, text, and blit operations; it deliberately has
no present, batch, plane-ownership, framebuffer-ownership, LCD, driver, HAL, or
platform operation. Retaining a surface pointer after callback return is an SDK
contract violation and every use outside that borrow returns a stale-handle
error when observable by the runtime.

An invalidation requested from inside `render` applies to a later render pass;
it is not consumed by the pass already in progress. Runtime schedules that
pending pass immediately rather than waiting for the next manifest tick.

Tick and render intervals/budgets come only from the immutable generated
descriptor. Apps cannot refresh them or create a catch-up loop. Timer events
and `tick(ctx, monotonic_now)` run synchronously on the existing firmware loop;
render runs only for a pending invalidation and runtime owns Display
begin/present/abort.

## Lifecycle teardown deadline access

The SDK declares:

```c
hk_result_t hk_app_context_teardown_deadline(
    const hk_app_context_t *ctx,
    hk_deadline_t *deadline);
```

During `stop` and app `cleanup`, this accessor returns the one finite absolute
monotonic deadline stored by App Runtime when teardown began. It returns
`HK_ERR_INVALID_STATE` outside teardown. Repeated calls return the same
`hk_deadline_t`; the accessor MUST NOT refresh, extend, partition, or derive a
per-stage or per-provider deadline.

Stop and cleanup code passes this value unchanged to deadline-aware capability
and service release operations. It does not call a raw clock, choose a hardware
timer, or acquire a second Time implementation. App Runtime creates the value
exactly once through public `hk_time_deadline_after_us`, its private runtime
owner, the one composed Time Capability provider, and a runtime-controlled
finite policy budget; the manifest and app cannot configure or extend it. An
already-expired value remains the required value for later owner-wide cleanup.

## Ownership and memory

Contexts, handles, surfaces, events, and state views are borrowed for the
lifetimes specified by App Runtime and the corresponding capability. Apps MUST
NOT retain a callback-scoped context or surface after the callback returns.
Every handle is scoped to the current app owner and generation and is invalid
after teardown. Handles remain valid during app `cleanup`; runtime then attempts
owner-wide cleanup before invalidating handles and context. Copying either the
context or a handle does not extend its lifetime or allow access to a later
generation.

Context owner and grant fields are snapshots only. The runtime's authoritative
owner and resolved availability are private and cannot be changed through the
SDK object; cleanup never trusts owner data read back from the callback
snapshot.

SDK functions are bounded and allocation-free. They do not create tasks,
queues, cores, general background work, or hidden heap storage. Large buffers
remain explicit borrows. App state is descriptor-sized fixed storage and is
reused only after the normative runtime teardown and generation invalidation.
The build-generated descriptor supplies manifest `state_bytes` and the fixed
`HK_APP_STATE_ALIGNMENT` ABI policy; alignment is not a runtime request or an
app-manifest tuning field.

## Host fake and portability

The SDK contract is platform-independent. Its deterministic host fake executes
the same public lifecycle, context, event, capability, service, cleanup, stale
generation, and limit cases without hardware. A fake may record operations but
MUST preserve owner, deadline, cancellation, buffer, and teardown semantics.

A native app is portable only when it builds against this entry surface and its
declared public capabilities. Successful compilation against SEN0305 private
headers is not SDK conformance.

## Compatibility

SDK `0.1.x` accepts App Runtime `0.1.x`, Native App Manifest `0.1.x` schema major
`1`, and Capability API `0.1.x`; every range is `[0.1.0, 0.2.0)`. An
experimental breaking change increments MINOR. Publishing this SDK does not
change Firmware `0.4.0`, HMPY `1.1.0`, Board Port `0.1.0`, Legacy App Lifecycle
`0.2.0`, or MicroPython API `1.0.0`.

The legacy adapter is runtime implementation, not a second public SDK. The one
foreground switch boundary is shared by lifecycle-v2 and legacy apps, so menu,
BACK, autostart, debug forced exit, safe-mode fallback, and rapid switching use
one close/unwind decision. A legacy
manifest `entry` names an app-owned const `hk_legacy_app_entry_t` binding object;
the generated private descriptor references it and the generic registry invokes
it. That type and binding are unavailable from `sdk/include`. Existing legacy
apps remain compatible while migrated apps use the SDK; all apps remain subject
to manifest composition and architecture guards.

## References

- [App Runtime](APP_RUNTIME.md)
- [Native App Manifest](APP_MANIFEST.md)
- [Capability API](CAPABILITY_API.md)
- [Versioning Policy](VERSIONING.md)
- [Architecture Vision](../ARCHITECTURE_VISION.md)
- [ADR-0007](../adr/0007-adopt-generation-checked-app-lifecycle.md)
