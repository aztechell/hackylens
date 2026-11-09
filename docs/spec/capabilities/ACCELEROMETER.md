---
contract-id: hackylens.capability.accelerometer
owner: platform-architecture
version: 0.1.0
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
