---
contract-id: hackylens.capability.input
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# Typed Input service

## Identity and features

- Numeric ID: `0x00010002`.
- Canonical name: `hackylens.cap.input`.

Feature bits:

| Bit | Name | Meaning |
|---:|---|---|
| `1 << 0` | `HK_INPUT_FEATURE_STATE` | Current stable logical state |
| `1 << 1` | `HK_INPUT_FEATURE_EVENTS` | Sequenced edge events |
| `1 << 2` | `HK_INPUT_FEATURE_DEBOUNCED_BUTTONS` | Time-based logical buttons |

These identifiers remain build metadata during S8; the direct binding does not
negotiate versions or features at runtime.

## Logical state and events

Logical button identifiers are public bit values independent of GPIO numbers,
electrical polarity, or board routes. The initial profile contains Left, OK,
Right, and Back. A platform mapping translates physical inputs to those logical
bits.

The public event shape is:

```c
typedef struct {
    uint64_t sequence;
    uint64_t timestamp_us;
    uint32_t state;
    uint32_t changed;
    uint32_t pressed;
    uint32_t released;
    uint32_t dropped;
} hk_input_event_t;
```

`state` is the complete stable state after the event. `changed` identifies all
bits changed by the event. `pressed = changed & state` and
`released = changed & ~state`. One accepted transition produces one event;
holding a button does not repeat an edge.

The runtime services the initial K210 provider on a 10 ms target cadence in the
existing cooperative superloop. Raw electrical sampling is gated to no more
than once per 10 ms; blocking core-0 work can delay a sample. The provider
accepts a transition after 20 ms of continuous stable raw state. Event timestamp
is the monotonic time at which the stable transition is accepted, not the first
raw bounce.

## Public operations

`hk_input_service()` returns the immutable board binding or NULL when absent.
Apps receive the same pointer through `hk_app_context_input(ctx, &input)`;
callback-context validation remains in the runtime. No generic lease is created.

- `hk_input_get_info(input, info)` reports supported bits and sample/debounce limits.
- `hk_input_get_state(input, state)` samples and returns stable state without
  consuming events. FILES also uses this between GIF rows to preserve short taps.
- `hk_input_cursor_open(input, cursor)` starts a caller-owned reader after the
  current sequence. `hk_input_cursor_close(cursor)` invalidates that reader only.
- `hk_input_next_event(input, cursor, event)` samples and returns the next event
  or `HK_PENDING`, without waiting for a future edge.

Each reader has its own sequence cursor. The native dispatcher holds one
persistent cursor; apps and MicroPython normally read state only. There is no
provider cursor-slot table or per-reader generation allocation.

## Bounded storage and overflow

The initial provider uses one explicit static ring of eight events. Its capacity
is reported by `hk_input_get_info`; the ring is fixed provider storage accounted
for by the Phase 2 static-RAM evidence, not an inventory limit. It is not a
hidden task or queue.

When a reader falls behind overwritten events, `hk_input_next_event` returns
`HK_ERR_OVERFLOW`. The output reports latest stable state and dropped
count (saturated at `UINT32_MAX`), and the cursor advances to the current sequence. Stale edges MUST NOT be
replayed after resynchronization.

## Lifetime, affinity, and cleanup

The binding is shared and read-only. Sampling is owned by the platform input
provider and runs in the existing superloop; no background task is created.
The initial K210 provider is `CORE0` affine.

Closing a cursor does not reset the global sampler, debounce state or event ring,
and does not consume events for another reader. Calls are serialized on core 0.

## Required resources and consumers

Build availability requires descriptor-backed logical button resources and a
supported raw sampler. A board with unbound physical pins MUST NOT advertise
input by guessing from its identity.

The native runtime dispatcher is the initial native consumer and adapts events
to the current `hk_input_snapshot_t` callbacks. MicroPython `buttons()` reads
the same provider.

## Fake and acceptance

The fake accepts timestamped raw samples and exposes deterministic advancement.
Tests cover bounce, press/release, simultaneous changes, hold behavior,
independent cursors, overflow/resync, no-event polling, cursor close/reopen, and the absent build binding.

SEN0305 acceptance physically exercises every logical button and records
debounce/event latency. Cube remains conformance-only until separately
qualified.

## References

- [Capability API](../CAPABILITY_API.md)
- [Current App Lifecycle](../../APP_LIFECYCLE.md)
- [MicroPython API](../../MICROPYTHON_API.md)
