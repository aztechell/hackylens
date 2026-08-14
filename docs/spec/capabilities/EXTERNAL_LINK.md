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

## Connector ownership and routing

One capability instance represents one physical connector. UART and I2C modes
that share pins, muxes, or peripherals MUST NOT be advertised as independently
ownable capabilities. The connector lease is exclusive.

Supported modes and routes are generated from the selected descriptor,
platform mapping, driver support, and provider implementation. Board ID,
connector label, or HMPY metadata MUST NOT be used to guess a mode.

Changing mode while an operation is active returns `HK_ERR_BUSY`. Mode cleanup
must quiesce and reset the previous peripheral before applying the new route.

## Configuration

UART version `0.1.0` is 8 data bits, no parity, one stop bit, with a provider-
reported baud range. I2C controller reports supported rates and accepts 7-bit
addresses only. I2C target reports its supported address and bounded buffer
limits.

Unsupported baud, rate, address, role, or feature returns
`HK_ERR_FEATURE_UNAVAILABLE` or `HK_ERR_INVALID_ARGUMENT` before routing or
peripheral side effects.

## Asynchronous operations

Potentially blocking transfers use a generation-checked `hk_external_op_t`:

- UART write begin;
- non-blocking UART read;
- I2C controller transfer begin, including combined write/read;
- operation poll;
- operation cancel;
- bounded I2C target poll/buffer operations.

There is at most one in-flight controller/transmit operation per exclusive
lease. `poll` performs no more than one provider-declared FIFO burst, initially
32 bytes, and returns `HK_PENDING` until terminal.

UART write succeeds only after every byte is accepted and both FIFO and shift
register are idle. A non-blocking UART read may succeed with zero bytes. I2C
NACK is `HK_ERR_IO`; deadline/cancel must stop and reset the controller before a
terminal result.

## Deadline and buffer ownership

The absolute deadline supplied at operation begin is stored in the operation
token and applies to the whole payload/transaction. It MUST NOT be reset for a
256-byte language-adapter chunk, FIFO burst, I2C phase, repeated start, or poll.

TX buffers are borrowed read-only until terminal completion. RX buffers are
borrowed writable and cannot be read before terminal completion except for an
explicit completed prefix reported by the operation. Cancellation MUST prevent
late UART/I2C writes after its terminal result.

## Native and language-adapter arbitration

The normal external-link protocol service is a native capability consumer. A
MicroPython run that needs raw connector access asks product policy to pause the
normal service; that service voluntarily releases its lease. After MicroPython
owner cleanup, the normal service reacquires and restores its configured HMPY
mode.

The capability core does not preempt owners or encode this product policy. The
MicroPython cross-core bridge contains transport/ticket/cancel logic only and
calls the same provider as the native service.

## Fake and acceptance

The fake records routing modes, FIFO progress, bytes, I2C phases, buffer
lifetime, original deadline, resets, cancellation, and late-effect attempts.
Tests cover mode conflicts, unsupported features, UART drain, non-blocking read,
I2C NACK/timeout, combined transfers, owner cleanup, and native/MicroPython
provider identity.

SEN0305 physical acceptance requires UART TX/RX loopback and a known 7-bit I2C
target, plus restoration of the normal external/HMPY service. Host tests alone
cannot close the electrical gate.

## References

- [Capability API](../CAPABILITY_API.md)
- [External Link Protocol](../../EXTERNAL_LINK_PROTOCOL.md)
- [MicroPython API](../../MICROPYTHON_API.md)
- [Board Port Contract](../BOARD_PORT.md)
