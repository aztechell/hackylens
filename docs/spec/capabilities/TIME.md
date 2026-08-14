---
contract-id: hackylens.capability.time
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# Time Capability

## Identity and features

- Numeric ID: `0x00010001`.
- Canonical name: `hackylens.cap.time`.

Feature bits:

| Bit | Name | Meaning |
|---:|---|---|
| `1 << 0` | `HK_TIME_FEATURE_MONOTONIC_US` | Monotonic microsecond clock |
| `1 << 1` | `HK_TIME_FEATURE_SLEEP_UNTIL` | Bounded cooperative sleep |

Version `0.1.0` requires both features.

## Public operations

- `hk_time_acquire(owner, request, handle)` obtains a shared lease.
- `hk_time_now_us(owner, handle, value)` returns the current monotonic value.
- `hk_time_deadline_after_us(owner, handle, duration_us, deadline)` creates an
  absolute deadline and rejects overflow or a duration above the published
  maximum.
- `hk_time_sleep_until(owner, handle, deadline, cancel)` sleeps cooperatively
  until the deadline or cancellation.

The monotonic value MUST NOT move backwards during one boot. It is not wall
clock time and has no calendar, timezone, or persistence semantics.

## Ownership, affinity, and timing

Time leases are shared. A provider may advertise `ANY_CORE` only when the same
monotonic domain and atomic read semantics are valid on every supported core.

`sleep_until` uses the caller's absolute deadline as both the requested wake
time and the operation deadline. It MUST check cancellation at least every
5 ms. It returns `HK_OK` when the wake time is reached and
`HK_ERR_CANCELLED` when cancellation is observed first. There is no infinite
sleep.

## Errors and cleanup

In addition to common errors:

- duration addition overflow returns `HK_ERR_LIMIT`;
- a deadline outside the provider's maximum sleep interval returns
  `HK_ERR_LIMIT`;
- a non-monotonic platform observation is `HK_ERR_INTERNAL` and quarantines the
  provider.

Release has no hardware side effect beyond invalidating the shared lease.

## Required resources and consumers

The platform mapping MUST provide one monotonic clock source. The source is a
platform property, not a board-ID inference.

Initial native consumers are Pong, camera photo timing, files, QR, Apriltag,
object detection, and Sleep. MicroPython `ticks_ms()` and `sleep_ms()` use the
same provider without changing MicroPython API v1.

## Fake and acceptance

The fake supports explicit clock advancement and deterministic cancellation at
chosen timestamps. Contract tests cover monotonicity, addition overflow,
already-expired deadlines, cancellation races, maximum duration, and shared
leases.

SEN0305 acceptance records read overhead and sleep/cancel latency. A Cube build
may prove compile conformance but does not claim clock runtime qualification.

## References

- [Capability API](../CAPABILITY_API.md)
- [MicroPython API](../../MICROPYTHON_API.md)

