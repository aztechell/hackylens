---
contract-id: hackylens.external-link
owner: device-protocols
version: 1.0.0
stability: experimental
wire-major: 1
---

# HackyLens External Link Protocol v1

This is a HackyLens-native protocol. It deliberately does not emulate the
original HUSKYLENS command set or packet layout.

## Physical port

The external connector is independent from the UART3 debug/PC connection.
Use the cable that matches the board. The SEN0305 has a four-pin Gravity
connector. The Maix Cube has a four-pin Grove connector with a 2.0 mm pitch
and different contact positions.

| SEN0305 Gravity | Maix Cube Grove | Descriptor route roles | UART mode | I2C mode |
|---|---|---|---|---|
| `T` | `25` | `IO_EXTERNAL_UART_T` / `IO_EXTERNAL_I2C_T` | UART1 TX, 9600/115200/1000000 8N1 | I2C0 SDA |
| `R` | `24` | `IO_EXTERNAL_UART_R` / `IO_EXTERNAL_I2C_R` | UART1 RX, 9600/115200/1000000 8N1 | I2C0 SCL |
| `-` | `G` | — | Ground | Ground |
| `+` (power input) | `V` (3.3 V) | — | Board-specific supply | Board-specific supply |

The selected board descriptor is the sole source of physical pin numbers.
Use 3.3 V logic. For a USB-powered I2C test fixture, connect SDA, SCL and the
common ground. Leave the connector supply disconnected. For SEN0305, DFRobot
specifies a 3.3–5.0 V supply range and typical consumption of 320 mA at 3.3 V or
230 mA at 5 V. The SEN0305 power selector gives USB priority over its `+` input.
See the [SEN0305 pinout](https://wiki.dfrobot.com/sen0305/#pinout) and the
[Maix Cube pinout](https://dl.sipeed.com/fileList/MAIX/HDK/Sipeed-Maix-Cube/ProductSpecification/Sipeed%20Maix%20Cube%20Datasheet%20V1.0.pdf#page=3).

I2C mode uses the camera as a 7-bit slave at address `0x32`. UART is the default.
Set **Settings → Connection → Grove Mode** to UART or I2C. **Grove Mode** is
the shared settings label for the external port on both boards. In UART mode,
the **Baud** field selects 9600, 115200, or 1000000 and saves the choice. The PC
debug console also accepts `HKLINKUART` and `HKLINKI2C`. `HKLINK9600`,
`HKLINK115200`, and `HKLINK1000000` select UART and save the requested speed.
`HKLINKINFO` reads the active mode, baud rate, and frame counters.

### SEN0305 UART bench wiring

Use a four-pin Gravity cable and 3.3 V logic. The keyed PH2.0 end plugs into the
SEN0305. Match the wires to the board labels.
[DFRobot Gravity cable](https://www.dfrobot.com/product-2261.html)

| SEN0305 Gravity | NodeMCU 1.0 (ESP8266) | 3.3 V USB–UART adapter |
|---|---|---|
| `T` (IO35, TX) | `D2` (GPIO4, RX) | `RXD` |
| `R` (IO34, RX) | `D1` (GPIO5, TX) | `TXD` |
| `-` (GND) | `GND` | `GND` |
| `+` | Leave open | Leave open |

The NodeMCU wiring uses the same three connections as the SEN0305 I2C fixture
(`D1` was SCL and `D2` was SDA). Power each board through its own USB cable.
Select UART mode and 115200 baud on the SEN0305 before starting the test. Use
115200 baud, 8 data bits, no parity, one stop bit, and no flow control on the
controller. The NodeMCU uses software UART on `D1` and `D2` and keeps its
hardware UART available for USB logging.

Send the PING request as raw bytes and compare the reply with the expected
PONG. This checks both directions through UART1.

| Direction | Hex bytes |
|---|---|
| Controller → SEN0305 | `48 4B 01 03 01 00 04 00 55 41 52 54 05 73` |
| SEN0305 → controller | `48 4B 01 83 01 00 04 00 55 41 52 54 B7 CC` |

## Frame

All multibyte integers are little-endian. UART and I2C carry exactly the same
frame:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | magic bytes `H`, `K` |
| 2 | 1 | protocol version, currently `1` |
| 3 | 1 | message type |
| 4 | 2 | caller-selected sequence number |
| 6 | 2 | payload length, maximum 160 bytes |
| 8 | N | payload |
| 8+N | 2 | CRC-16/CCITT-FALSE over bytes 2 through 7+N |

CRC parameters are polynomial `0x1021`, initial value `0xFFFF`, no reflection,
and no final XOR.

UART is a byte stream and the device resynchronizes on `HK`. For I2C, the master
writes one complete request transaction with STOP. Wait 20 ms before the
first 8-byte header read. If all eight bytes are zero, retry the header in
a separate READ without rewriting the request. Keep one request outstanding
and use a response deadline. Display updates and loading a model from microSD
can delay the response. The master must also honor clock stretching during
I2C transfers.

When the header arrives, check the magic, version, sequence and payload length.
Read the declared payload plus the 2-byte CRC. The response cursor continues
across multiple I2C read transactions, allowing masters with 32-byte Wire
buffers to read in chunks.
A new write with a payload discards any unread portion of the previous
response. A valid frame prepares a new response at byte zero. Reads before
the response is ready and bytes clocked after it is complete return zero.

## Commands

| Request | Response | Payload |
|---|---|---|
| `0x01 GET_INFO` | `0x81 INFO` | none |
| `0x02 GET_RESULTS` | `0x82 RESULTS` | none |
| `0x03 PING` | `0x83 PONG` | arbitrary bytes, echoed |
| Unsupported type or unexpected payload on a valid frame | `0xFF ERROR` | one-byte error code |

Frames with an invalid header, length, version, or CRC are dropped.

`INFO` contains `capabilities:u16`, `max_items:u8`, `active_transport:u8`
(`0=UART`, `1=I2C`), `i2c_address:u8`, one reserved byte, and `uart_baud:u32`.
Capability bits 0–3 mean UART, I2C, BLOCK, and ARROW.

## Results and unified items

`RESULTS` starts with `frame_id:u32`, `source:u8`, `count:u8`, `width:u16`, and
`height:u16`. Source `0` is no active result, `1` is face detection, `2` is QR,
`3` is AprilTag, `4` is VOC20 object detection, `5` is face recognition, and
`255` is reserved for user algorithms.

Each result is a fixed 16-byte item:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | kind: `1=BLOCK`, `2=ARROW` |
| 1 | 1 | flags, AprilTag Hamming distance or zero |
| 2 | 2 | algorithm-local ID |
| 4 | 2 | x0 |
| 6 | 2 | y0 |
| 8 | 2 | x1 |
| 10 | 2 | y1 |
| 12 | 2 | confidence, 0–1000 |
| 14 | 2 | reserved, zero |

For a BLOCK, `(x0,y0)` and `(x1,y1)` are the top-left and bottom-right corners.
For an ARROW they are the start and end points. Coordinates use the `width` by
`height` source canvas returned in the same message. FACE DETECT currently
publishes up to eight BLOCK items and clears them on app exit. OBJECT DETECT
uses the VOC20 class ID `0..19` and reports
confidence on the existing `0..1000` scale; the item layout is unchanged.
OBJECT DETECT publishes only classes enabled in **OBJECT SETTINGS / Classes**.
QR publishes a bounding BLOCK for a decoded code with ID and confidence set
to zero. Its payload bytes are available through the
[local C and MicroPython API](MICROPYTHON_API.md#vision-results).
FACE RECOG uses ID zero for unknown faces and 1–10 for learned faces.
Confidence is the nonnegative cosine similarity scaled to 0–1000.

APRILTAG returns its latest snapshot in response to `GET_RESULTS` over
UART or I2C. Both NEAR and FAR report the native tag ID, Hamming distance,
confidence and bounding rectangle in the 320×240 capture. The app's Output
setting controls whether the snapshot includes all detections or selected IDs.
The USB debug command `HKTAGINFO` reports optical and horizontal range in
millimeters. External Link v1 carries the bounding rectangle and tag metadata.

## SEN0305 UART hardware verification

The NodeMCU 1.0 ran a software UART probe from RAM on the SEN0305 Gravity
port. Its cycle counter was calibrated against elapsed PC time. The probe
used the 104 MHz setting to capture and decode signal edges at 1 Mbaud.

| Baud | Valid PING replies |
|---:|---:|
| 9600 | 300/300 |
| 115200 | 300/300 |
| 1000000 | 300/300 |

Every reply passed sequence, payload, and CRC checks. Each run used payloads
from 0 to 160 bytes and included 50 replies at the full 170-byte frame size.
At each speed, `GET_INFO` reported the selected baud rate,
`GET_RESULTS` returned an empty snapshot from the menu, and the protocol handled
unsupported requests, bad CRCs, and resynchronization after stray bytes.
With QR-CAMERA running at 640×480, each speed also passed 20 of 20 full-size
replies. `GET_RESULTS` reported changing QR frame numbers during the run.

The original 9600-baud transmit path used `TFL` to decide how much of the
eight-byte UART1 FIFO was free. Register sampling on SEN0305 showed `TFL`
returning zero during transmission, allowing a refill before the previous
batch had drained. GPIO captures recovered 140 and 142 bytes from two expected
170-byte replies. The v0.9.0 driver queued one FIFO-sized batch at a time
and used the configured 8N1 baud rate to schedule the next write. A full batch
took approximately 8.34 ms at 9600 baud.

## SEN0305 I2C hardware verification

A NodeMCU 1.0 (ESP8266) controller connected to the SEN0305 Gravity port
completed 114 exchanges at approximately 6.4 kHz, 1,140 at approximately 64 kHz,
and 1,368 at approximately 262 kHz. The two faster runs continued through
automatic sleep. In ten further trials the controller interrupted a read,
issued nine clock pulses and a STOP, and then received a valid response to a
new request without restarting the SEN0305.

The SCL rates use the 52 MHz cycle-counter frequency measured with the original
RAM image. The original timing report assumed 80 MHz and overstated the rates.

On SEN0305, replayed input frames were processed by the native QR, AprilTag,
FACE DETECT, FACE RECOG, and OBJECT DETECT apps. Each app sent populated
`GET_RESULTS` replies over the physical I2C connector at approximately 64
and 262 kHz. Leaving each app cleared the result. The replay exercised result
production and transport with known inputs. The Maix Cube physical I2C
and UART checks over SP-MOD are in the [Cube hardware log](MAIX_CUBE_TEST_LOG.md#uart-and-i2c-over-sp-mod-2026-11-22).
Those results cover the board's UART1 and I2C0 driver and protocol paths.
