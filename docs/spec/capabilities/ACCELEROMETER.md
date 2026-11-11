---
contract-id: hackylens.capability.accelerometer
owner: platform-architecture
version: 0.2.0
stability: experimental
---

# Accelerometer service

Numeric ID `0x00010007`, canonical name `hackylens.cap.accelerometer`.
`hk_accelerometer_service()` returns a board-lifetime read-only binding, or
`NULL` on boards that lack a provider. Native apps declare `accelerometer` in
their manifest and obtain the binding using `hk_app_context_accelerometer()`
in a lifecycle callback. No session or teardown is needed.

`hk_accelerometer_read()` runs on core 0 and returns one signed X/Y/Z sample in
milli-g, including gravity, using the sensor's native axes. It only writes the
sample on `HK_OK`. The initial configuration is lazy and returns `HK_PENDING`
while the sensor settles. Missing bindings return `HK_ERR_CAPABILITY_ABSENT`.
Unexpected part IDs return `HK_ERR_FEATURE_UNAVAILABLE` and transport failures
return `HK_ERR_IO`. MicroPython reaches the same provider over its core-0 RPC.

Maix Cube uses the [MSA301](https://wiki.sipeed.com/soft/maixpy/en/develop_kit_board/maix_cube.html)
at seven-bit address `0x26` on the internal I²C1 bus, at ±2 g, 14 bits and
62.5 Hz. The provider reads all six axis bytes in one transaction. SEN0305
has no accelerometer binding.

`hk_accelerometer_read_motion()` runs on core 0 and reads the optional
hardware motion status. Its first successful call configures MSA301 event
detection with a 250 ms status latch and no changes to INT1 routing.
The provider reports activity, single tap, double tap, free fall and
orientation change. Activity uses a 250 mg threshold on each axis. Flags
can repeat in consecutive polls. The orientation value uses native sensor
axes and becomes meaningful after an orientation event. The output changes
only on `HK_OK`. A provider without the motion reader returns
`HK_ERR_FEATURE_UNAVAILABLE`.

The MSA301 also supports selectable ±4/8/16 g ranges, lower resolutions,
power modes, selectable sample rates and data-ready interrupts. See the
[MEMSensing datasheet](https://download.kamami.pl/p585252-MSA301.pdf).
