---
contract-id: hackylens.capability.display
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# Display Capability

## Identity and features

- Numeric ID: `0x00010003`.
- Canonical name: `hackylens.cap.display`.

Feature bits:

| Bit | Name | Meaning |
|---:|---|---|
| `1 << 0` | `HK_DISPLAY_FEATURE_BASE_PLANE` | Native base plane |
| `1 << 1` | `HK_DISPLAY_FEATURE_OVERLAY_PLANE` | Independent overlay plane |
| `1 << 2` | `HK_DISPLAY_FEATURE_BATCH` | Bounded retained batch |
| `1 << 3` | `HK_DISPLAY_FEATURE_DIRTY_REGIONS` | Partial present |
| `1 << 4` | `HK_DISPLAY_FEATURE_RGB565` | RGB565 pixel views |
| `1 << 5` | `HK_DISPLAY_FEATURE_BORROWED_SURFACE` | Writable borrowed surface |
| `1 << 6` | `HK_DISPLAY_FEATURE_TEXT` | Bounded text command |

## Coordinates, rectangles, and clipping

The logical origin is top-left. X grows right and Y grows down. Rectangles are
half-open: `[x, x + width)` and `[y, y + height)`. Public coordinates are signed
32-bit values; dimensions are unsigned 32-bit values.

All arithmetic MUST be checked before conversion to driver coordinates. An
overflow returns `HK_ERR_INVALID_ARGUMENT`. A valid rectangle wholly outside
the active clip is a successful no-op. Partly visible primitives and pixel
views are clipped to the intersection.

## Info, planes, and ownership

`hk_display_info_t` reports width, height, pixel formats, planes, alignment,
maximum commands, text bytes, dirty rectangles, borrowed surfaces, transfer
slice, and maximum present duration.

`BASE` and `OVERLAY` are separate exclusive resource planes. At most one owner
leases each plane. Base and overlay MAY be leased concurrently when the overlay
feature is present. Version `0.1.0` does not define arbitrary layers, z-order,
alpha blending, or a general compositor.

## Batch operations

The public batch lifecycle is:

1. acquire a display plane;
2. `begin_batch`;
3. add bounded clear, fill-rectangle, rectangle, text, or pixel-blit commands;
4. optionally set a clip or mark additional dirty rectangles;
5. `present` with one absolute deadline and cancellation token;
6. retry the same staged batch after a terminal transfer error/cancel, or
   `abort` it;
7. release the plane.

Primitive parameters and bounded text are copied into declared static batch
storage. Large pixel views are borrowed until terminal present or abort.
Limits are checked before mutating the staged batch.

Dirty regions are derived from clipped commands and explicit marks. A provider
may merge overlapping regions to remain within its advertised limit, but when
`HK_DISPLAY_FEATURE_DIRTY_REGIONS` is present it MUST NOT silently promote an
incremental batch to full-screen merely for implementation convenience.

## Borrowed full-frame surface

`surface_acquire` returns an implementation-owned writable
`hk_buffer_view_t` with explicit format, stride, size, and alignment. The caller
may access it only until surface present, abort, or plane release. The caller
MUST mark every modified region.

The provider MUST NOT allocate a second full framebuffer implicitly. Additional
full-frame storage is a separate advertised resource and is absent from the
initial K210 provider.

## Present, cancellation, and repair

One caller deadline applies to every dirty region, row, transport chunk, and
retry attempt inside that invocation. The provider checks cancellation between
bounded transfer slices.

A successful present atomically advances the logical committed batch. Physical
panels may expose partial progress while a transfer is running. If present is
cancelled or fails after physical progress:

- the staged batch remains available for retry or abort;
- the previous logical committed state remains identified;
- the provider records the affected damage as `needs_repair`;
- no late panel writes occur after the terminal result;
- the next successful present or bounded release cleanup repairs the affected
  region before claiming a consistent committed display.

Overlay release discards its staged/committed overlay and restores the current
base content. Cleanup uses the caller's release deadline and is not cancellable.

## Required resources and consumers

Inventory presence requires a selected display device, supported driver,
complete routing/preparation, and a provider that can report its dimensions and
formats. Apps MUST use runtime info or explicit capability limits rather than
infer 320x240 from a board ID.

Native views use `BASE`. MicroPython display operations use `OVERLAY`. Camera
and files use a borrowed full-frame surface where appropriate. Pong uses
bounded primitive batches and dirty regions.

## Fake and acceptance

The fake logs plane ownership, commands, clips, dirty regions, transferred
bytes, slices, deadlines, cancellation, committed state, and repair state. The
same contract suite runs against the K210 adapter with stubbed panel transport.

SEN0305 acceptance covers native screens, full-frame camera/files, Pong dirty
frames, MicroPython overlay, cancellation/retry, cleanup, and restoration. A
full present MUST fit its advertised maximum, initially 500 ms.

## References

- [Capability API](../CAPABILITY_API.md)
- [MicroPython API](../../MICROPYTHON_API.md)

