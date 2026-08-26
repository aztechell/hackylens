# Phase 3 Resource and Dispatch Baseline

## Identity

Phase 3 starts from the exact Phase 2 closure commit
`ed2adcebb757ccf4c8bdaf5b7ba3f0b9c596eedb` (tag `v0.4.0`) and its exact
implementation commit `f8b76f441d25a7be02bf8c804736750306b8f2b7`. The canonical machine-readable
record is [phase3-baseline.json](evidence/phase3-baseline.json). It pins the Phase
2 closure and automated-result hashes, toolchain, full and
MicroPython-disabled artifact/resource identities, and the legacy dispatch
source used for the matched host baseline.

The full closure profile occupies 1,544,192 erase-rounded flash bytes and
2,857,752 static RAM bytes (`data + bss`). The MicroPython-disabled profile
occupies 1,351,680 erase-rounded flash bytes and 2,580,760 static RAM bytes.
Both are reproduced from the Phase 2 automated result, not inferred from a
later binary name.

## Approved Phase 3 budgets

All deltas are measured independently for the matching full or
MicroPython-disabled Phase 2 closure profile.

| Resource | Phase 3 limit |
|---|---:|
| Erase-rounded flash growth | `65,536 B` |
| Static RAM growth | `16,384 B` |
| Host dispatch overhead p99 | `<= 100 us` absolute and `<= 50 us` above matched baseline |
| SEN0305 runtime dispatch overhead p99 | `<= 100 us`, callback/provider work excluded |
| Runtime compiler stack frame | `<= 32,768 B` |
| New heap allocation sites | `0` |
| New background tasks | `0` |
| New general queues | `0` |
| New runtime cores | `0` |
| Additional full framebuffers | `0` |

The flash allowance is sixteen 4 KiB erase sectors for App Runtime, immutable
descriptors, SDK/service frontends, fakes that enter firmware, and the three
migration proofs. The RAM allowance is deliberately much smaller than one
320x240 RGB565 framebuffer and must contain all new fixed runtime tables, tokens,
service state, and app-state growth. It does not authorize padding, a second
framebuffer, or moving dynamic storage into static arrays merely to avoid the
heap guard. Raising either allowance requires architecture/resource review and
an updated masterplan before implementation is accepted.

The dispatch baseline measures legacy input lookup plus one empty callback
through the actual Phase 2 `hk_dispatch.c`; callback body and provider I/O are
excluded. Ten local runs of 101 samples by 1,000 iterations produced a maximum
per-run p99 of 10 ns. This host observation is diagnostic rather than K210
timing. The portable gate therefore uses conservative absolute and delta
ceilings; lifecycle implementation later adds matched v2 and SEN0305 evidence.

The five zero-resource limits are source-difference gates against the exact
Phase 2 closure commit, not declarations inferred from static-RAM size. The
checker fingerprints direct and transitively wrapped heap, task, and queue
creation sites, counts runtime core starts, and counts full-display framebuffer
allocation expressions. Renames of byte-identical source are tolerated; a new
site or a delete/add replacement is not. Both profile receipts and the current
full artifact are checked before the zero-resource result is accepted.

## Reproduction

With the pinned host compiler and dependencies available:

```powershell
python tools/check_phase3_baseline.py --measure-dispatch
python tools/build_firmware.py full --board huskylens-sen0305 --disable-app micropython
python tools/check_phase3_baseline.py --verify-profile micropython-disabled
python tools/check_phase2_resources.py --capture-profile micropython-disabled
python tools/build_firmware.py full --board huskylens-sen0305
python tools/check_phase3_baseline.py --verify-profile full
python tools/check_phase2_resources.py --capture-profile full
python tools/check_phase3_baseline.py --verify-resources
```

The checker validates canonical JSON, Git ancestry, historical closure bytes,
the immutable Phase 2 evidence chain, normalized dispatch source/harness hashes,
attested board/profile identity, formulas, numerical limits, zero-resource
source deltas, build receipts, and current full artifact identity. CI executes
the same checks after both required profile receipts exist.

## Hardware impact

Package 3.1 changes documentation, host guards, evidence, and CI only. It does
not change firmware source, generated firmware inputs, composition, binary, or
runtime behavior. No targeted physical test is required; existing Phase 2
observations remain attached to their original images rather than being
relabelled as Phase 3 hardware evidence.
