---
contract-id: hackylens.capability.external-link
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# External Link Capability

## Identity and features

- Numeric ID: `0x00010004`.
- Canonical name: `hackylens.cap.external-link`.

Feature bits:

| Bit | Name | Meaning |
|---:|---|---|
| `1 << 0` | `HK_EXTERNAL_LINK_FEATURE_UART` | Raw UART mode |
| `1 << 1` | `HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER` | 7-bit I2C controller |
| `1 << 2` | `HK_EXTERNAL_LINK_FEATURE_I2C_TARGET` | I2C target mode |

The public ABI is
`firmware/include/hackylens/capability/external_link.h`. All extensible input
structures use version `1`, require zero reserved input fields, and follow the
common size/version rules in the Capability API.

## Acquisition, connector ownership, and routing

`hk_external_link_acquire` takes `mode_features`, the complete set of modes the
lease intends to use. Those bits are added to `request.required_features`
during capability negotiation. Trying to configure a mode outside that
negotiated set returns `HK_ERR_NOT_DECLARED`; a negotiated feature unsupported
by the selected provider returns `HK_ERR_FEATURE_UNAVAILABLE`.

One capability instance represents one physical connector. UART and I2C modes
that share pins, muxes, or peripherals MUST NOT be advertised as independently
ownable capabilities. There is one exclusive connector lease, even when the
provider supports all three modes.

Supported modes and routes are generated from the selected descriptor,
platform mapping, driver support, and provider implementation. Board ID,
connector label, or HMPY metadata MUST NOT be used to guess a mode.

Changing mode while an operation is active returns `HK_ERR_BUSY`. Mode cleanup
MUST quiesce and reset the previous peripheral before applying the new route.
Validation completes before either cleanup or routing side effects. Public
mode values are `UNCONFIGURED`, `UART`, `I2C_CONTROLLER`, and `I2C_TARGET`;
private route IDs and peripheral instances are not public ABI.

## Configuration

UART version `0.1.0` is 8 data bits, no parity, one stop bit. Its baud must be
within the provider-reported inclusive range. I2C controller frequency must be
within its provider-reported inclusive range, and transfers accept 7-bit
addresses only. I2C target configuration also accepts only a 7-bit address.
The info structure reports fixed upper bounds for controller write/read and
target receive/response buffers.

Malformed configuration, out-of-range baud/frequency/address, non-zero
reserved fields, and unknown feature bits return `HK_ERR_INVALID_ARGUMENT`
before routing or peripheral side effects. A structurally valid mode absent
from the provider returns `HK_ERR_FEATURE_UNAVAILABLE`.

## Operation token and state machine

Potentially blocking transfers use a generation-checked
`hk_external_link_op_t`. The all-zero value is no operation. A successful
begin returns `HK_PENDING` and a non-zero token. `poll` and `cancel` validate
the owner, connector lease, and token generation before observing hardware.
There is one in-flight operation per lease across both operation kinds:

- UART write begin;
- I2C controller transfer begin, including combined write/read.

The normative states are:

| State | Allowed action | Result |
|---|---|---|
| no operation | begin | `HK_PENDING` with a fresh generation |
| in flight | another begin or mode change | `HK_ERR_BUSY`, no side effects |
| in flight | poll | at most 32 bytes of total TX/RX progress, then `HK_PENDING` or terminal |
| in flight | cancel | quiesce/reset, then terminal `HK_ERR_CANCELLED` |
| terminal latched | poll or cancel | the same terminal result and progress |
| terminal observed | next begin | retires the old generation and starts a fresh operation |

A copied old token returns `HK_ERR_STALE_HANDLE` after the next successful
begin. Progress reports accepted TX bytes and the completed RX prefix. A
provider MAY report a smaller `maximum_poll_bytes`, but version 0.1 MUST process
at most 32 bytes across all phases in one `poll` call.

Argument/owner/token errors do not alter the operation. Terminal results are
latched. Release and trusted owner cleanup quiesce any active operation,
invalidate the token and lease, and return borrowed buffers only after hardware
is safe.

## UART semantics

UART write progress counts bytes accepted by the provider. Acceptance of the
last byte is not success: terminal `HK_OK` requires an empty FIFO and an idle
shift register. While those conditions are pending, progress includes
`HK_EXTERNAL_LINK_PROGRESS_UART_DRAINING` and the operation remains borrowed.

`hk_external_link_uart_read` is synchronous and non-blocking. It copies no more
than the provider poll bound and may return `HK_OK` with zero bytes.

## I2C controller and target semantics

An I2C controller operation is one transaction consisting of its optional
write phase, optional repeated start, and optional read phase. At least one
phase must be non-empty. The 32-byte poll bound applies to the sum of write and
read progress. NACK maps to `HK_ERR_IO`; NACK, timeout, and cancellation stop
and reset the controller before returning terminal.

I2C target polling is synchronous and non-blocking. It returns `HK_PENDING`
when no event is queued. A write event is consumed only when the caller's
writable buffer can hold the complete bounded payload. A read event reports the
requested byte count. `hk_external_link_i2c_target_respond` copies a bounded
readable response before returning, so it does not retain the caller's buffer.

## Deadline and buffer ownership

The original absolute deadline supplied at operation begin is stored with the
operation and applies to the whole payload/transaction. It MUST NOT be reset
for a 256-byte language-adapter chunk, FIFO burst, I2C phase, repeated start, or
poll. A finite deadline already expired at begin fails before hardware side
effects. `HK_DEADLINE_IMMEDIATE` permits only progress available without
waiting; if that attempt is not terminal, the operation terminates with
`HK_ERR_DEADLINE_EXCEEDED`. `UINT64_MAX` is invalid.

Validation and terminal precedence follow the common contract: arguments and
ownership, an already-latched terminal result, cancellation, deadline, then
progress/I/O. Thus cancellation wins over deadline expiry when both are first
observed in one poll.

TX buffers are borrowed read-only until terminal completion. RX buffers are
borrowed writable and cannot be read before terminal completion except for the
completed RX prefix reported by `rx_completed_bytes`; that prefix is stable and
readable. The cancel view is also borrowed through terminal. After terminal
cancellation, timeout, release, or owner cleanup, the provider MUST NOT perform
late writes to UART, I2C, or the caller's RX buffer.

## Phase 2.10 implementation boundary

The normal external-link protocol service is a native capability consumer. A
MicroPython run that needs raw connector access asks product policy to pause the
normal service; that service voluntarily releases its lease. After MicroPython
owner cleanup, the normal service reacquires and restores its configured HMPY
mode.

The capability core does not preempt owners or encode this product policy. The
MicroPython cross-core bridge contains transport/ticket/cancel logic only and
calls the same provider as the native service. Those K210 provider and consumer
migrations belong to Phase 2.10; Phase 2.9 does not change production external
service, HAL, routing, or MicroPython runtime behavior.

## Fake and acceptance

The fixed-capacity fake records routing modes, FIFO progress, bytes, I2C phases,
buffer lifetime, original deadline, resets, cancellation, terminal results,
and late-effect attempts. Tests cover exclusive conflicts, unsupported
features/modes, active mode switching, UART partial progress/drain and
non-blocking read, combined I2C transfer, NACK/timeout, cancellation before and
during an operation, stale generations, target buffers, release cleanup, and no
late effects. It allocates no heap, task, queue, or unbounded event storage.

SEN0305 physical acceptance requires UART TX/RX loopback and a known 7-bit I2C
target, plus restoration of the normal external/HMPY service. Host tests alone
cannot close the electrical gate; that later physical gate is not Phase 2.9
completion evidence.

## References

- [Capability API](../CAPABILITY_API.md)
- [External Link Protocol](../../EXTERNAL_LINK_PROTOCOL.md)
- [MicroPython API](../../MICROPYTHON_API.md)
- [Board Port Contract](../BOARD_PORT.md)
