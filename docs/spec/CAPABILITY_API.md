---
contract-id: hackylens.capability-api
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# HackyLens typed hardware services

The public headers remain under `firmware/include/hackylens/capability/`.
These are direct typed services. The path name does not imply runtime discovery,
version negotiation or grant injection. [Architecture](../ARCHITECTURE.md)
describes their integration.

## Bindings and availability

Time, Input, Lights, Display and External Link each have one immutable platform
binding. Native code and MicroPython use the same production implementation.
Build-time board resources and app requirements select compatible sources;
an absent optional service has a null accessor. The build does not emit a
generic provider inventory or owner grants. Runtime does not parse manifests,
register providers, negotiate versions or infer hardware from a board ID.

Camera and Storage remain existing typed services and do not acquire a generic
wrapper. Board/BSP routes and platform HAL stay private to their layers.

## Lifetimes and conflicts

| Service | Binding and mutable state |
|---|---|
| [Time](capabilities/TIME.md) | Board-lifetime immutable binding; bounded deadline/cancellation operations |
| [Input](capabilities/INPUT.md) | Shared sampler/debounce/event ring; caller-owned reader cursors |
| [Lights](capabilities/LIGHTS.md) | Stable channel sessions; channel overlap rejected; safe-off and local quarantine |
| [Display](capabilities/DISPLAY.md) | Stable BASE/OVERLAY sessions; explicit transactions and buffer borrows |
| [External Link](capabilities/EXTERNAL_LINK.md) | Exclusive connector session; mode state, incremental operations, cancellation and IRQ handoff |

A session must not be copied or moved while open: providers identify its stable
address. Ordinary close may retain a session for retry after failure. Forced
retirement always clears logical ownership and outstanding borrows; an unsafe
cleanup quarantines the affected resource. A copied session cannot release the
original claim. Async operation generations remain independent of session
storage and must not alias old operations after reopening the same address.

Immutable Time/Input bindings do not need per-call lease generations. Context,
frame, surface, workspace, External Link operation and core1 generations remain
where they reject actual stale work. This migration does not relax Camera
frame borrows or executor handoff.

## Results, deadlines and memory

`common.h` provides shared results, absolute monotonic deadlines, cancellation
and explicit buffer views. Service headers define their actual operations and
supported features. Compile-time API metadata and typed information queries do
not create a runtime version-negotiation mechanism.

Teardown creates one finite absolute deadline and passes it unchanged through
stop and every scoped retirement. Failure or expiry does not skip later
cleanup, logical invalidation or state retirement. The first error is retained;
quarantine remains local to the failed resource. See [App Runtime](APP_RUNTIME.md).

Operations remain bounded and use fixed storage. Large buffers have explicit
borrows and capacities. Service APIs do not silently add a heap, background
queue, task, framebuffer or second hardware implementation.

## Layering and verification

Apps and language adapters use public typed headers or existing permitted
portable firmware services. They do not include provider-private, driver,
board/BSP, HAL or K210 SDK headers. Implementations do not depend on apps or
language adapters. A Python-only hardware provider is not an accepted boundary.

Behavioral suites exercise fake and K210 bindings with the same service cases;
platform-specific tests remain supplemental. Build checks verify board routes,
source inclusion and compiled provider evidence. Compiling a second board does
not prove its physical behavior or general portability.

Hardware checks follow changed paths. An unrelated accepted observation need
not be repeated. UART session lifecycle without an external peer does not
establish physical UART/I2C data exchange. Full and MicroPython-disabled builds,
resource measurements and CI validate changes to these services.

## References

- [Feature App SDK](APP_SDK.md)
- [App Runtime](APP_RUNTIME.md)
- [Native App Manifest](APP_MANIFEST.md)
- [Board Port](BOARD_PORT.md)
- [Specification authority](README.md)
