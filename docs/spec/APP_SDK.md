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
after teardown.

SDK functions are bounded and allocation-free. They do not create tasks,
queues, cores, general background work, or hidden heap storage. Large buffers
remain explicit borrows. App state is descriptor-sized fixed storage and is
reused only after the normative runtime teardown and generation invalidation.

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

The legacy adapter is runtime implementation, not a second public SDK. A legacy
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
