# Current Project State

> HackyLens v0.4 is a layered K210 reference firmware and MicroPython technology
> preview.

## Phase 1 status

Firmware 0.4.0 implements the experimental Board Port Contract 0.1.0. The
common firmware is composed with the K210 platform and exactly one selected
board support package. Build, image, package, release, and flash commands
require an explicit `--board`; there is no implicit board.

Full builds emit canonical private build attestations bound to the exact image
hash, version, selected board/platform/runtime profile, target, and complete app
composition. `make_image.py` and `package_release.py` require this evidence;
only the complete unmodified full build is release-qualified. Feature-disabled
or fault-injection images cannot be promoted by adding version/board text or by
renaming the file. The attestation is private build evidence, not a public
contract or runtime-discovery surface.

Two descriptors are tracked:

- `huskylens-sen0305` is the runtime-qualified, releaseable port using the
  `hackylens-full` runtime profile.
- `sipeed-maix-cube` is a descriptor/BSP compile-conformance port. It is not
  runtime-qualified or releaseable, and its full build, package, release, and
  flash operations fail before performing work.

The Cube port proves that a second descriptor and minimal BSP satisfy the
conformance compile/link harness. It does not prove that common runtime
firmware is composed for Cube, nor is it evidence of Cube runtime behavior or
general hardware independence.

## Implemented boundaries

- Board descriptors own identity, available services, routes, defaults, flash
  layout, and programming metadata.
- One private `build/generated/board_config.h` is generated at firmware build
  time from the selected `board.toml`.
- Applications do not include K210 SDK headers. BSP/HAL code and explicitly
  platform-bound internals may include them; startup lives in
  `platforms/k210/startup`.
- Applications may continue to call board-independent driver/service APIs in
  Phase 1, but cannot include a board BSP, platform HAL, or K210 SDK header.
- Application time needs use the public `hackylens.cap.time` 0.1 capability;
  runtime and MicroPython button reads share `hackylens.cap.input` 0.1; and
  settings, camera sessions, Sleep, and MicroPython light writes share
  `hackylens.cap.lights` 0.1. The native external-link service and MicroPython
  UART/I2C bindings share `hackylens.cap.external-link` 0.1. Private
  boot/recovery wiring remains outside the capability surface.
- Private schema-2 app requirements and the separate runtime/service/adapter
  consumer manifest control board-aware composition, capability grants,
  optional fallbacks, and exclusion. They do not expose runtime hardware
  access. `--require-app` changes an incompatible exclusion into an error.
- Phase 2.2 adds Capability API 0.1 common types and a fixed-capacity host-tested
  owner/lease core. Phase 2.3 generates its immutable provider table and owner
  grants from the selected board/profile and the private K210 capability map,
  without changing `hk_app_t`. Phase 2.4 adds Time with shared native/MicroPython
  deadline and cancellation semantics. Phase 2.5 adds Input with fixed-time
  sampling/debounce, an eight-event ring, independent lease cursors, and one
  state provider for the native dispatcher and MicroPython. Phase 2.6 adds
  non-overlapping channel-mask ownership for backlight, illumination, and RGB,
  safe-off cleanup, and persisted settings restoration after temporary leases.
  Phase 2.7 freezes Display 0.1 semantics with a deterministic fixed-capacity
  host fake, including planes, clipped batches, borrowed surfaces, dirty-region
  transfer accounting, retry, repair, and cleanup. Phase 2.8 adds the K210
  provider, splits the raw ST7789 transport from plane state, migrates native
  views through a private typed BASE binding, and moves MicroPython drawing to
  OVERLAY. Both planes reuse the single shadow framebuffer. Phase 2.9 freezes
  the public External Link 0.1 connector/async contract with a deterministic
  fixed-capacity fake. Phase 2.10 adds the K210 provider, migrates the native
  UART/I2C-target service and MicroPython UART/I2C-controller bindings to that
  single provider, and removes the Python-only hardware implementation.
  Phase 2.11 is complete: raw SD access remains behind the
  private storage boundary, frame storage is a portable service with explicit
  borrow/release, and architecture guard v2 has an explicit layer map plus
  generated dependency and object-symbol evidence. Corrective commit `5eaf827`
  generalizes repository object-symbol edges, makes workspace generation
  exhaustion non-wrapping, and passed
  [Release firmware run 32565126272](https://github.com/aztechell/hackylens/actions/runs/32565126272).
  Phase 2.12 automated qualification is complete: all five capabilities run
  identical provider-independent normative case sets against fake and K210,
  with adapter-specific checks kept supplemental. The complete build,
  diagnostic, resource, latency, and rolling-evidence matrix passed
  [Release firmware run 32569674140](https://github.com/aztechell/hackylens/actions/runs/32569674140).
  Phase 2 is complete. Exact implementation commit `f8b76f4` passed automated
  qualification, and owner acceptance links the tested SEN0305 image to the
  immutable candidate result through the impact-based physical ledger. Buttons,
  Files, MicroPython, and Lights received the final targeted checks; unchanged
  UART, I2C, TIME, HMPY, camera, Pong, menu, and Sleep observations were carried
  forward only after impact review. Evidence limitations remain explicit; Maix
  Cube is compile-conformance-only. See `docs/PHASE2_PHYSICAL_STATUS.md`.

The twelve feature applications remain self-contained source modules with
compile-time exclusion. Existing camera, LCD, input, storage, HMPY, external
link, AI, and MicroPython behavior remains part of the SEN0305 runtime profile.

## Contract and release status

| Area | Status |
| --- | --- |
| Firmware | 0.4.0; Phase 2 Capability Platform completed on SEN0305 |
| Board Port Contract | 0.1.0 experimental |
| HMPY Contract | 1.1.0 experimental; wire-major 1 |
| Versioning Policy | 0.5.0 |
| SEN0305 runtime port | Supported and releaseable |
| Cube port | Compile conformance only |
| Capability build composition | Phase 2 qualified on SEN0305; Time + Input + Display + External Link + Lights runtime providers |
| App Runtime / Native App Manifest / Feature App SDK | App Runtime and Feature App SDK `0.2.0` experimental; Native App Manifest remains `0.1.0` schema major `1`; minimal build-time `app.toml` manifests drive production sources and one immutable build-directory registry; the production lifecycle-v2 engine uses `start`/`event`/`render`/`stop` with timer-as-event, runtime-owned Display transactions, generation-checked wakeups, and one temporary legacy adapter |
| General hardware portability | Not claimed |

`HELLO.board` is the canonical `board.toml.id`; clients must not infer
capabilities from it. Capability discovery returns the generated immutable
inventory, currently Time, Input, Display, External Link, and Lights on SEN0305,
and is not an App Runtime or manifest surface. The private generated
`runtime_supported` marker describes
Board Port runtime eligibility only; it is not physical capability or hardware
qualification.

## Verification and evidence

Host checks cover descriptor/profile/programming validation, route selection
and collisions, flash overlap/overflow, architecture boundaries, board-aware
composition, sidecar safety, and CLI rejection paths. CI compile-checks the Cube BSP and builds SEN0305 full and
feature-disabled configurations.

Phase 3.1 adds contract/version/scope negatives, generic SDK/runtime/manifest/
generated-registry layer rules, and the reproducible
[Phase 3 baseline](PHASE3_BASELINE.md). Its full and MicroPython-disabled
resource deltas against the exact Phase 2 closure are both zero; host legacy
dispatch reproduction remains below the approved 100 us p99 ceiling. This
governance package changes no firmware behavior and requires no physical rerun.
Its CI gate also source-diffs the exact closure and rejects every new heap,
background-task, general-queue, runtime-core, or full-framebuffer creation site.

Phase 3.2 defines the complete strict Native App Manifest schema and provides a
build-time-only validator with path-independent canonical JSON output. CI runs
it against committed lifecycle-v2 and legacy positive fixtures. Host negatives
cover unknown/missing fields, version/range/fallback rules, finite resource
ceilings, app-scoped service namespaces, real-directory path confinement, and
ID/entry/autostart/menu/generated-symbol collisions. Phase 3.2 itself left app
composition, the manual registry, and firmware behavior unchanged.

Phase 3.3 adds manifests for all twelve current feature apps and makes their
canonical in-memory model the only production input for app source/include
staging, `HK_ENABLE_APP_*`, capability requests, and transitional legacy
driver-availability exclusions. The separate `firmware/app_requirements.toml`
and manual app/source maps are removed. Generated composition/default files are
deterministic and freshness-checked; representative disabled builds prove app
private sources and app-specific third-party inputs leave staging. The manual
runtime registry was intentionally retained for replacement in Phase 3.4, so
Phase 3.3 did not change firmware runtime behavior or require a physical rerun.

Phase 3.4 replaces that manual descriptor table with deterministic immutable
generated descriptors and menu views. Each current app owns one explicit const
legacy entry binding named by its manifest; generic core code contains no
concrete app table and performs lookup/iteration/dispatch. Stable persisted
autostart IDs do not depend on menu or array order. Host fixtures cover empty,
single, all-enabled, disabled, and mixed legacy/v2 composition, plus current
screen, menu, autostart, SD, debug, tick, capability, and service parity.

Phase 3.6 adds the public fixed-capacity `hk_app_context_t` SDK surface and the
private runtime's exact manifest-grant preflight/injection path. Required
absence or version/feature mismatch excludes an app before `probe`; optional
absence exposes only its manifest fallback. One owner scopes every injected
capability/service handle from `prepare` through app cleanup, after which
owner-wide cleanup precedes generation invalidation and state reuse. Host fake
inventory tests cover undeclared access, partial injection unwind, owner
exhaustion, stable preflight availability, callback-snapshot corruption, and
stale copied contexts/handles. Lifecycle callbacks receive a const context;
the authoritative owner and resolved availability remain private, so callback
corruption cannot suppress owner-wide cleanup or change injected grants. Phase
3.6 itself left the engine unwired from production and required no physical
rerun.

Phase 3.7 connects the private engine to the existing firmware loop without
migrating a production app. One fixed-capacity switch now owns legacy/v2 open,
BACK, autostart, debug-forced exit, safe-mode fallback, and failure unwind.
Input events come from the existing composed Input provider; SD/media events
come from the existing controller path. Tick and render budgets use the
composed Time provider, and the opaque SDK surface stages bounded operations on
the injected Display handle while runtime alone presents or aborts. Host tests
cover mixed switching, ordered close/input/media/timer/wakeup events, rapid
switch, BACK during start, timeout, render/present failure, autostart fallback,
surface invalidation, and stale wakeup rejection. Callback failure retains its
original error, emits one callback-failed close event, and still completes the
common teardown. Render-time invalidation schedules an immediate later pass.
An executable host harness runs the production `hk_main.c` and
`app_runtime_integration.c` boundary with deterministic provider fakes,
including v2 open/input/media/wakeup/tick/render/close and legacy open/close.
At the time of that wiring, no production app used the v2 branch yet.

Phase 3.8 publishes the complete Feature App SDK core through the canonical
`hackylens/app.h` umbrella. Lifecycle callbacks and `hk_app_v2_entry_t` now live
in the public SDK rather than a private runtime header; existing Capability API
handles are reused directly. Host tests compile that production runtime with
typed Time, Input, and Display doubles; there is no public AppHostFake or second
lifecycle engine. The SDK closure guard rejects private/platform/provider
headers and enforces SDK-only repository dependencies for lifecycle-v2 app
sources. That package did not migrate a production app or add a second hardware
path.

Simplification package S6 keeps that production runtime in place and shrinks the
public v2 lifecycle to `start`/`event`/`render`/`stop`. BUTTONS and PONG now use
that API with fixed-capacity app state, input/timer events, and runtime-owned
Display transactions. The other ten production apps remain on one temporary
legacy adapter.

The pinned pre-change resource baseline is recorded in
`docs/evidence/phase1-baseline.json`. Its exact canonical bytes are digest-
locked to SHA-256
`75d8300766266c144847d1805541ca2303a1fffd1b4496649e50f38af3bb889f`,
its commit must be an ancestor of HEAD, and the historical/current
bootstrap pins and historical `VERSION` must match the evidence. The baseline
BIN/ELF hashes are explicitly named manually pinned golden evidence, and the
CMake version is explicitly operator-recorded; none is presented as a
reproduced build attestation. Acceptance
requires no static-RAM growth and no more than 8192 bytes of erase-rounded
flash growth for the historical Phase 1 closure. The lexical runtime-object guard
applies translation-phase splicing, removes comments and literals, tracks
direct primitives, cross-translation-unit explicit aliases/macros,
allocation/task/queue wrapper calls, file-scope creation, and C++ creation
forms, then compares site-aware fingerprints against the complete baseline
source snapshot. It is a conservative lexical policy check rather than a full
C/C++ semantic analyser, so alias-name propagation may intentionally reject
ambiguous same-named calls. Result hashes are explicitly
named `local_sha256` under `hash_scope=local-workspace-diagnostic`; they remain
mandatory local evidence but are excluded from CI freshness because Phase 1
does not claim general reproducible builds. Compiler and assembler prefix maps
normalize workspace and SDK roots before compilation; builds from differently
sized workspace paths have identical raw BIN bytes and identical resource
sections, which makes the exact CI resource projection path-independent. The
debug ELF hash remains diagnostic rather than a release identity.

`docs/evidence/phase1-result.json` records the Phase 1 rolling result at Phase 1
closure and is no longer compared byte-for-byte with later Phase 2 binaries.
Phase 2 CI instead checks both current build profiles against the immutable
`phase2-baseline.json` attestation and resource budgets. The immutable Phase 1
closure snapshot is `docs/evidence/phase1-closure-result.json`. The hardware-smoke
document names that closure file, pins its canonical-file SHA-256, and must
match its board, firmware version, image size, and image SHA-256. Consequently,
later rolling results do not inherit the closure image's hardware-tested status.

The physical SEN0305 gate passed on 2026-08-13 and is recorded in
`docs/evidence/phase1-hardware-smoke.json`. The release-qualified package booted
as firmware 0.3.0; display and camera captures were inspected; all four button
masks, illumination, RGB, SD, HMPY, external-link runtime routing/transmit, and
package/flash round trip passed. The ISP stub has no flash-read command, and no
external electrical loopback fixture was attached, so those limitations are
explicit in the evidence rather than being presented as qualifications. Cube
hardware qualification is a separate future gate.

## Remaining architecture work

Display is runtime-composed; its borrowed BASE surface is explicitly an in-place
backing store, while retained command batches remain transactional. Phase 2
records physical display observations and explicitly retains the missing
matched-workload timing dataset as an evidence limitation rather than inventing
measurements. Later packages may extend qualification on new hardware.
Public App Runtime, Native App Manifest, and Feature App SDK contracts are fixed
at `0.1.0 experimental` by the Phase 3.1 governance baseline. Native manifest
schema validation, production manifests, generated build composition, the
generated registry/legacy adapter, the private lifecycle-v2 state machine, its
public context/capability injection, and the production foreground switching,
event, tick, and render integration are implemented. BUTTONS and PONG already
use the minimal v2 lifecycle; remaining production-app migrations and legacy
adapter removal remain later Phase 3 work.
Public storage, camera, vision, and AI capabilities also remain Phase 3+.

Other product gaps remain unchanged: broader MicroPython hardware APIs,
full project/package management, multi-project IDE workflows, formal
Python-to-native migration, complete original-firmware parity, and long-run
hardware qualification.
