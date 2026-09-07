---
contract-id: hackylens.capability.time
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# Typed Time service

S8 replaces the experimental lease-based Time source interface with a direct
immutable binding. Native apps and MicroPython still use the same production
clock and bounded-sleep implementation. Other services retain their broker
until their own migration. The old Time acquire/release/owner interface is
removed rather than retained as a compatibility layer.

## Binding and operations

`hk_time_service()` returns the statically selected `const hk_time_t *`.
The firmware runtime requires Time; composition rejects an absent binding.
App code receives it through
`hk_app_context_time(ctx, &time)`. The opaque handle contains no generic lease,
owner generation or runtime version/feature negotiation. Build composition
checks required service availability before compilation.

- `hk_time_now_us(time, value)` reads monotonic microseconds.
- `hk_time_deadline_after_us(time, duration_us, deadline)` creates an absolute
  deadline, rejecting overflow and durations above `HK_TIME_MAX_SLEEP_US`.
- `hk_time_sleep_until(time, wake_target, operation_deadline, cancel)` waits in
  bounded slices with cancellation and an unchanged absolute operation deadline.

The monotonic domain is shared across cores during one boot. It is not wall
clock time and has no calendar, timezone or persistence semantics. Reading the
K210 clock and protecting its monotonic state remain under the same spinlock.
The binding has firmware lifetime and needs no release or owner cleanup.

## Timing and failure semantics

The maximum interval is 300 seconds. Sleep checks cancellation and deadline at
least every 5 ms. Wake target and operation deadline are distinct; the latter
may precede the former. Neither is extended between slices.

An already reached wake target succeeds. During sleep, reaching the target
wins over cancellation; otherwise cancellation is checked before an expired
operation deadline. Cancellation returns `HK_ERR_CANCELLED`, expired operation
deadline returns `HK_ERR_DEADLINE_EXCEEDED`, and overflowing or excessive
intervals return `HK_ERR_LIMIT`.

A successful nonzero sleep must advance the observed clock. Frozen or backward
clock observations fail instead of causing an unbounded loop. Fault state is
local to the Time provider, with cross-core synchronization; it does not depend
on a generic owner table. This preserves failed-clock behavior without a lease
quarantine mechanism.

## Consumers and checks

Runtime, Files, Pong, camera photo timing, QR, detectors, auto-sleep and
MicroPython share this service. MicroPython `ticks_ms()` and `sleep_ms()` keep
API v1 behavior and use the same Time implementation as native apps.

Fake and K210-backed host suites cover monotonicity, overflow, maximum interval,
expired targets/deadlines, cancellation races, bounded slices, progress/fault
handling and clock locking. Time must link without the generic capability core
and must not allocate memory or create a task, queue or core. Hardware checks
record actual device behavior; host tests do not imply board qualification.

## References

- [Capability API during migration](../CAPABILITY_API.md)
- [App Runtime](../APP_RUNTIME.md)
- [MicroPython API](../../MICROPYTHON_API.md)
