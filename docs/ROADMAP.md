# Roadmap

HackyLens targets useful SEN0305 firmware and measurable reuse of application
logic. The current implementation, acceptance and limitations are in
[Architecture](ARCHITECTURE.md); interface details are in [technical references](spec/README.md).

## Next product and research work

- Improve SEN0305 features around concrete use cases, including heavy GIF
  playback and input latency where measurements show a bottleneck.
- Physically qualify a second K210 board with unchanged applications. Record
  exactly which BSP, service or application changes the port requires; Cube
  compile conformance alone is not physical portability evidence.
- Extend MicroPython only for demanded scenarios through existing shared typed
  services. Camera/KPU/vision are not part of the current Python API.
- Compare paired Python/native applications on common workloads: behavior,
  migration effort, flash/RAM, latency distributions and cleanup under failure.
  Report hardware, toolchain and workload limits alongside results.

Choose the next increment from the actual use case and available hardware.
A Q1 paper needs a testable claim and comparative evidence; architecture size
or the mere existence of C and Python APIs does not establish that claim.

## Deferred until needed

Project Format, package/on-device Program Manager, dynamic native loading,
Python-to-native generators, multi-project IDE expansion and a separate
conformance ecosystem are deferred. The existing IDE/HMPY workflow remains.
Original-firmware feature parity is not a prerequisite for testing the
architecture hypothesis.

## Development constraints

Keep one native lifecycle, one app manifest source, explicit board selection
and shared native/Python hardware implementations. Preserve MicroPython API v1,
HMPY, persisted IDs/settings and actual resource lifetimes. Validate changed
paths and resource costs; carry forward independent hardware observations.
Ordinary changes need code, tests and current documentation, not phase plans,
ADR templates or additional evidence schemas.
