# K210 implementation mapping

Informative mapping of firmware 0.9.0 at [baseline commit](https://github.com/aztechell/hackylens/commit/15acdd519874a09645d036b036f37273462fbe09) to the [candidate specification](../spec/v0.1/README.md). This is not a passing conformance report. Existing [architecture](../ARCHITECTURE.md) and [technical references](../spec/README.md) remain authoritative for concrete APIs.

| K210 choice | Portable requirement |
| --- | --- |
| Core 0 services/UI, core 1 VM/decoder | Serialized authority and bounded handoff, not core count |
| app.toml, generated C registry | Static validated semantic composition |
| MicroPython embed, 128 KiB GC heap | Declared finite engine resources |
| Single-slot RPC, uncached alias, barriers | Synchronized bounded handoff |
| Stable-address sessions and generation tokens | Non-aliasing ownership and lifetime |
| BASE/OVERLAY, fixed framebuffer | Declared features and transaction semantics |
| WDT1 reset | Independent recovery and autostart suppression |
| DVP, KPU, frame arena | Explicit borrows and shared services |
| littlefs, FAT32, HMPY | Separate storage and wire contracts |

## Traceability

Candidate evidence sources only; no tests were run for this mapping. Paths are repository-relative.

| Requirements | Existing contracts and code | Candidate tests/evidence |
| --- | --- | --- |
| ARC | docs/ARCHITECTURE.md; docs/RAM_BUDGET.md; tools/architecture_layers.toml | tests/test_app_architecture.py; tests/test_service_architecture.py; tests/test_resources.py; tests/test_app_sdk.py |
| CMP | docs/spec/APP_MANIFEST.md; tools/app_manifest.py; tools/app_composition.py; tools/app_registry.py | tests/test_app_manifest.py; tests/test_app_composition.py; tests/test_app_registry.py |
| LIF | docs/spec/APP_RUNTIME.md; firmware/src/app_runtime/runtime.c; sdk/include/hackylens/app/runtime.h; firmware/src/runtime/app_runtime_integration.c | tests/test_app_runtime_v2.py; tests/app_runtime_v2_harness.c; tests/app_runtime_mixed_harness.c; tests/app_runtime_production_harness.c |
| OWN | docs/spec/CAPABILITY_API.md; docs/spec/APP_SDK.md; firmware/src/services/frame_pool.c; frame_workspace.h; media_workspace.h | tests/test_camera_stream_lease.py; tests/test_shared_buffer.py; tests/test_app_runtime_v2.py |
| TIM, ERR, S-TIME | docs/spec/capabilities/TIME.md; firmware/include/hackylens/capability/common.h | tests/test_time_capability.py; tests/time_capability_harness.c |
| S-INPUT | docs/spec/capabilities/INPUT.md | tests/test_input_capability.py; tests/input_capability_harness.c |
| S-LIGHT | docs/spec/capabilities/LIGHTS.md | tests/test_lights_capability.py; tests/lights_capability_harness.c |
| S-DISP, S-RENDER | docs/spec/capabilities/DISPLAY.md; docs/spec/APP_RUNTIME.md | tests/display_normative_suite.c; tests/test_display_contract.py; tests/app_runtime_display_harness.c |
| S-LINK | docs/spec/capabilities/EXTERNAL_LINK.md | tests/external_link_normative_suite.c; tests/test_external_link_contract.py |
| S-POWER | docs/spec/capabilities/POWER.md | tests/test_power_off.py; tests/test_power_bus.py |
| S-ACCEL | docs/spec/capabilities/ACCELEROMETER.md | tests/test_accelerometer.py |
| SCR | docs/MICROPYTHON_API.md; docs/MICROPYTHON_RESEARCH.md; firmware/src/services/micropython_runtime.c; firmware/src/services/core1_executor.c; firmware/src/apps/micropython/micropython_bindings.c | tests/test_micropython_runtime.py; tests/test_micropython_bindings.py; tests/test_service_bindings.py; tests/test_micropython_app.py |
| REC | docs/WDT_HARDWARE_ACCEPTANCE.md | historical physical WDT1 test on both boards, 0.8.0-wdtfi at c57c81d; tests/test_hmpy_acceptance.py |

## Open conformance questions

- Core permits headless devices; current production integration opens BASE before start. Do not claim headless K210 qualification without testing.
- Cooperative deadlines do not preempt arbitrary native code. Audit finite provider steps, cancellation and exceptional cleanup; sampled host latency is not WCET.
- Native generations, script run IDs and operation tokens have different lifetimes. Script run ID wraps; verify non-aliasing of outstanding references rather than assuming all counters never wrap.
- Verify physical quiescence or quarantine of reachable borrowed storage after failed retirement. Logical invalidation alone is insufficient.
- Verify shared service semantics across native/script, not equal BASE/OVERLAY latency.
- Historical WDT hardware acceptance uses an earlier fault-injection build. Repeat for the claimed revision and publish raw logs where available. Reset hash preservation does not prove arbitrary power-cut consistency.
- Camera, vision, data and audio operation schemas are not yet frozen in the architecture specification. Existing SDK/wire contracts remain independent.
- Both physical boards use K210; fake/K210 normative tests share production code. No second SoC or independent runtime has been qualified.
- The new profile definitions and generalized requirements are proposed, not automatically proven by existing tests. Map each applicable requirement to actual pass/fail evidence using the [conformance plan](../spec/v0.1/CONFORMANCE.md).

## Research evidence

[UART Logger](../UART_LOGGER_STUDY.md), [Object Gate](../OBJECT_GATE_STUDY.md) and [Sky Glide](../SKY_GLIDE_STUDY.md) measure native/script workloads; [raw data](../research/) are separate. [Python-to-C](../PYTHON_TO_C.md) documents manual migration. These support research, not complete conformance. No particular benchmark ratio or RPC batching implementation is required by the specification.
