# Roadmap

HackyLens targets useful firmware for SEN0305 and Maix Cube and measurable
reuse of application logic. See [Architecture](ARCHITECTURE.md) for the current
implementation, hardware checks and limitations, and the
[technical references](spec/README.md) for interface details.

## Next product and research work

- Improve SEN0305 features around concrete use cases, including heavy GIF
  playback and input latency where measurements show a bottleneck.
- Document what the Maix Cube port changed in the BSP, shared services and
  applications, and which application logic it reused. Use the
  [Cube hardware log](MAIX_CUBE.md) to tie observations to tested builds.
- Extend MicroPython only for demanded scenarios through existing shared typed
  services. Camera/KPU/vision are not part of the current Python API.
- Run equivalent MicroPython and native C applications on both boards using the
  same workloads. Compare behavior, migration effort, flash and RAM use,
  latency distributions and cleanup after failures. Report hardware, toolchain
  and workload limits alongside results.

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
