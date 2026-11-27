# HackyLens conformance observations

The [v0.1 conformance plan](../docs/spec/v0.1/CONFORMANCE.md) defines 20 scenarios.
This suite currently scores host observations for CT-02, CT-03, CT-04, CT-05,
CT-06, CT-07, CT-08, CT-09, CT-11, CT-14, CT-17, CT-18 and CT-20.
The [requirement matrix](../docs/spec/v0.1/requirements.csv) determines which
requirements appear in each report. Current reports can expose violations and
record partial coverage. Full Core qualification requires additional cases,
structural evidence and target observations.

## Reproduce the host observations

Run from the repository root with Python 3.11 or newer and a host C compiler.
Set `CC` to select a compiler compatible with GCC-style options. Give collection
a new output directory for each run.

```text
python -m conformance.runner.run --check-matrix
python -m conformance.adapters.k210.collect build/conformance/k210-host
python -m conformance.runner.run build/conformance/k210-host
python -m conformance.mutations.time build/conformance/time-mutations
python -m conformance.mutations.lifecycle build/conformance/lifecycle-mutations
python -m conformance.mutations.external_link build/conformance/external-link-mutations
python -m conformance.mutations.shared_script build/conformance/shared-script-mutations
```

The collector compiles the K210 Time provider against a controlled clock and
wait interface. It compiles the production native runtime with test providers
and hooks for scope preparation and cleanup. Lights uses the K210 binding
with observable driver effects. Composition runs the production manifest
loader and service selection against controlled inputs. It compiles the
resulting registry and observes runtime lookup of persisted identities. The
connector fixture runs the production provider against a simulated peripheral
that holds caller receive storage until completion or driver stop. Shared
Script uses the production capability bridge and the same Lights provider as
native code. A controlled scheduler exercises admission, cancellation and
cleanup. A separate runtime fixture supplies a VM stub and delays the
executor's acknowledgement independently of worker exit. The adapters emit JSONL events
with arguments, clock samples, observed effects and result categories. The
Python runner consumes the bundle by itself. It never invokes the firmware,
imports the collector or interprets a firmware test banner as an outcome.

Exit code 0 means the observations in the requested bundle passed. Code 1
means a check identified a violation. Code 2 means required input did not
support a verdict. Reports retain separate requirement and observation
statuses. A requirement can remain `NOT RUN` when an observation passes.

## Bundle format

- [Manifest schema](schema/implementation_manifest.schema.json) describes the
  implementation, revision, source variant, environment, evaluated scopes and
  limits. `changed_inputs` lists fingerprinted inputs that differ from `HEAD`.
- `traces/<fixture-id>.jsonl` contains the JSON objects observed in the fixture.
  Each trace carries observations for one invocation with fresh test state.
- [Result schema](schema/result.schema.json) describes requirement results,
  independent check verdicts and SHA-256 evidence references. Both JSON and
  a Markdown view are written into the bundle.

Text fingerprints use LF line endings. Fixture and runner hashes distinguish
the suite revision from the build inputs. The [Time fixtures](fixtures/time.json)
specify clock stimuli, requests and independent outcomes. The
[composition fixtures](fixtures/composition.json) prescribe build inputs and
expected registry and admission observations. The
[lifecycle fixtures](fixtures/lifecycle.json), [Lights fixtures](fixtures/lights.json),
[External Link fixtures](fixtures/external_link.json), [Shared Script fixtures](fixtures/shared_script.json)
and [script runtime fixtures](fixtures/script_runtime.json) list actions and
required external observations. The repeated Shared Script case checks that
one pending request blocks another and that both script-owned light channels
can be reclaimed after each of 32 cycles. An adapter for
another implementation can emit the same events and use the scorer with its
own manifest and limits.

Time, lifecycle, connector and Shared Script mutations are compiled from temporary source
copies. Each experiment scores the unmodified build before measuring
detection. The summary
records expected failures, observed failures and the source fingerprint for
each mutation. The output also contains the control, verdicts and raw traces.

## Current evidence

The [K210 baseline report](reports/k210-0.9.0-host-baseline/report.md) records a
runtime-close event timestamp of zero after an earlier event in the same
instance, violating `LIF-03`. It also records a rejected script start replacing
a prior exception with `busy` before executor acknowledgement, violating
`ERR-02`. The
[working-tree candidate report](reports/k210-0.9.0-host-timestamp-candidate/report.md)
repeats the same observations after both fixes. The host tests use simulated
peripherals and controlled scheduling. Physical recovery and board behavior
still require target observations.

The [K210 target build record](reports/k210-0.9.0-target-builds.json) identifies
the SEN0305 and Maix Cube images by SHA-256. It records the base revision,
four staged source overrides, the toolchain, declared limits, selected
bindings and per-app descriptor limits. Generate it from the checkout used
for the cross-builds after building both targets.

```text
python -m conformance.adapters.k210.describe_build <checkout> <output.json>
```
