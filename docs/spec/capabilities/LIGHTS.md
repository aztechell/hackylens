---
contract-id: hackylens.capability.lights
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# Typed Lights service

## Identity and features

- Numeric ID: `0x00010005`.
- Canonical name: `hackylens.cap.lights`.

Feature and resource-mask bits:

| Bit | Name | Meaning |
|---:|---|---|
| `1 << 0` | `HK_LIGHTS_CHANNEL_BACKLIGHT` | Display backlight |
| `1 << 1` | `HK_LIGHTS_CHANNEL_ILLUMINATION` | Forward illumination |
| `1 << 2` | `HK_LIGHTS_CHANNEL_RGB` | RGB indicator |

## Ownership and values

Acquisition requests a non-zero channel mask. Ownership is exclusive only for
overlapping channel bits. Two owners MAY hold non-overlapping masks. Acquisition
is all-or-nothing; a conflict on one requested bit returns `HK_ERR_BUSY` without
granting another bit.

Public scalar and RGB channel values use the normalized inclusive range
`0..1000`. Language adapters convert their existing public range without
changing their API contract. Providers clamp neither invalid values nor absent
channels; they return an error before hardware access.

## Binding and sessions

`hk_lights_service()` returns the immutable board binding or NULL when absent.
Build metadata still describes channel availability; there are no runtime
version/feature requests or generic Lights leases.

`hk_lights_t` is a caller-owned channel session. Its address must remain stable
until it is closed or retired. Copying its fields does not transfer channel
ownership. Three channel claimant pointers enforce the actual conflicts; there
is no generic owner/generation or provider lease-slot table.

- `hk_lights_open(service, channels, session)` claims channels atomically.
- `hk_lights_get_info(service, info)` reports supported channels and limits.
- `hk_lights_set_level(session, channel, level, deadline, cancel)` writes a scalar.
- `hk_lights_set_rgb(session, r, g, b, deadline, cancel)` writes RGB.
- `hk_lights_close(session, deadline)` performs ordinary, retryable cleanup.
- `hk_lights_retire(session, deadline)` ends the session scope even on failure.

Native SDK apps acquire runtime-owned sessions through
`hk_app_context_lights(ctx, channels, &session)`. Runtime storage is bounded by
the three physical channels and is retired during teardown even if app stop
fails. Settings, camera and the MicroPython bridge have explicit stable sessions
and cleanup paths. Calls remain synchronous and serialized on core 0.

Writes validate channel ownership, values, cancellation and deadline before
hardware effects. A completed synchronous write is not rolled back by later
cancellation.

## Cleanup and policy

Ordinary close drives claimed channels to safe-off and releases them. An expired
finite deadline preserves the session for a bounded retry without hardware
effects. Scope retirement instead always invalidates the session; any channel
whose safe-off failed is quarantined and cannot be reacquired. Cleanup attempts
all affected channels using the same original deadline and preserves the first
error. No fresh deadline is created after a preceding failure.

Runtime fallback retires camera/native sessions and attempts every scoped
retirement even on failure. It does not depend on the camera UI
light-active flag. Persistent settings sessions are outside app teardown.

MicroPython sessions belong to the bridge run, including external HMPY runs.
Closing the native MicroPython screen only requests stop; it must not release
channels while the worker can still access them. Retirement occurs at the
existing terminal worker handoff or submit failure. Lights, Display and External Link cleanup
share one deadline and all cleanup paths are attempted.

The provider handles safe-off, while the settings service reclaims channels and
restores persisted values after temporary camera or MicroPython use. A failed
ordinary close must not discard the only retryable session.

## Required resources and consumers

Each advertised channel requires descriptor-backed resources, driver support,
and a provider mapping. A board may advertise any subset; no subset is inferred
from board identity.

Initial native consumers are settings/camera light services and Sleep.
MicroPython LED/RGB operations use the same provider.

## Fake and acceptance

The fake records channel ownership, writes, safe-off cleanup, persisted-service
reapplication, cancellation, and deadlines. Tests cover overlapping and
non-overlapping masks, all-or-nothing acquisition, unsupported channels, ranges,
copied-session rejection, cleanup, and native/MicroPython provider identity.

SEN0305 acceptance observes backlight, illumination, RGB, cleanup, and persisted
state restoration.

## References

- [Capability API](../CAPABILITY_API.md)
- [MicroPython API](../../MICROPYTHON_API.md)
