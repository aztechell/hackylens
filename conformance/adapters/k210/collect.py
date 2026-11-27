"""Build K210 implementation components on the host and record neutral traces."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

from conformance.adapters.k210 import composition_trace


ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
TIME_SOURCE = ROOT / "firmware/src/capabilities/time.c"
RUNTIME_SOURCE = ROOT / "firmware/src/app_runtime/runtime.c"
LINK_PROVIDER_SOURCE = ROOT / "platforms/k210/capabilities/external_link_adapter.c"
SCRIPT_BRIDGE_SOURCE = ROOT / "firmware/src/adapters/micropython/micropython_capability_bridge.c"
FIXTURE = ROOT / "conformance/fixtures/time.json"
LIFECYCLE_FIXTURE = ROOT / "conformance/fixtures/lifecycle.json"
LIGHTS_FIXTURE = ROOT / "conformance/fixtures/lights.json"
LINK_FIXTURE = ROOT / "conformance/fixtures/external_link.json"
SCRIPT_FIXTURE = ROOT / "conformance/fixtures/shared_script.json"
SCRIPT_RUNTIME_FIXTURE = ROOT / "conformance/fixtures/script_runtime.json"
TIME_LIMITS = {"max_interval_us": 300_000_000, "max_slice_us": 5_000}
LIFECYCLE_SOURCES = (
    "conformance/adapters/k210/lifecycle_trace.c",
    "firmware/src/app_runtime/runtime.c",
    "firmware/src/app_runtime/surface.c",
    "firmware/src/app_runtime/switch.c",
    "firmware/src/capabilities/time.c",
    "firmware/src/capabilities/lights.c",
    "firmware/src/capabilities/display.c",
    "firmware/src/capabilities/input.c",
    "firmware/src/capabilities/input_state.c",
    "tests/time_normative_fake_backend.c",
    "tests/lights_normative_fake_backend.c",
    "tests/input_normative_fake_backend.c",
    "tests/capability_fake_display.c",
    "tests/capability_fake_external_link.c",
    "tests/app_runtime_host_support.c",
)


def fixture_args(case: dict) -> list[str]:
    operation = case["operation"]
    if operation == "sleep":
        cancel = case["cancel_at_us"]
        return [operation, str(case["start_us"]), str(case["target_us"]),
                str(case["deadline_us"]), str(cancel) if cancel is not None else "none",
                "freeze" if case["freeze"] else "advance"]
    if operation == "read":
        return [operation, *(str(sample) for sample in case["samples_us"])]
    if operation == "deadline":
        interval = case.get("interval_us")
        if case.get("interval") == "max":
            interval = TIME_LIMITS["max_interval_us"]
        if case.get("interval") == "over_max":
            interval = TIME_LIMITS["max_interval_us"] + 1
        return [operation, str(case["start_us"]), str(interval)]
    raise ValueError(f"unknown fixture operation {operation}")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def run_cases(executable: Path, cases: list[dict], fixture_arguments, traces: Path) -> None:
    for case in cases:
        result = subprocess.run([str(executable), *fixture_arguments(case)], cwd=ROOT,
                                capture_output=True, check=True, timeout=5, text=True, encoding="utf-8")
        (traces / f"{case['id']}.jsonl").write_text(result.stdout, encoding="utf-8", newline="\n")


def compile_trace(compiler: str, flags: list[str], executable: Path,
                  includes: tuple[Path, ...], sources) -> None:
    subprocess.run([
        compiler, *flags, *(f"-I{path}" for path in includes), *(str(path) for path in sources),
        "-o", str(executable),
    ], cwd=ROOT, check=True, timeout=60)


def collect(destination: Path, *, time_only: bool = False,
            source_overrides: dict[Path, Path] | None = None) -> None:
    source_overrides = source_overrides or {}
    eligible = ({TIME_SOURCE} if time_only
                else {TIME_SOURCE, RUNTIME_SOURCE, LINK_PROVIDER_SOURCE, SCRIPT_BRIDGE_SOURCE})
    if not source_overrides.keys() <= eligible:
        raise ValueError("source substitution is outside the collected components")
    time_source = source_overrides.get(TIME_SOURCE, TIME_SOURCE)
    compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
    if not compiler:
        raise RuntimeError("a host C compiler is required")
    trace_dir = destination / "traces"
    trace_dir.mkdir(parents=True, exist_ok=False)
    cases = json.loads(FIXTURE.read_text(encoding="utf-8"))
    stubs = ROOT / "tests/k210_time_adapter_stubs"
    include = ROOT / "firmware/include"
    capabilities = ROOT / "firmware/src/capabilities"
    inputs = {
        "conformance/adapters/k210/collect.py": Path(__file__),
        "conformance/adapters/k210/time_trace.c": HERE / "time_trace.c",
        "firmware/src/capabilities/time.c": time_source,
        "firmware/src/capabilities/time_provider.h": capabilities / "time_provider.h",
        "firmware/include/hackylens/capability/time.h": include / "hackylens/capability/time.h",
        "firmware/include/hackylens/capability/common.h": include / "hackylens/capability/common.h",
        "platforms/k210/capabilities/time_adapter.c": ROOT / "platforms/k210/capabilities/time_adapter.c",
        "platforms/k210/hal/hal_time.h": ROOT / "platforms/k210/hal/hal_time.h",
        "tests/k210_time_adapter_stubs/atomic.h": stubs / "atomic.h",
        "tests/k210_time_adapter_stubs/sleep.h": stubs / "sleep.h",
    }
    sources = [inputs[name] for name in (
        "conformance/adapters/k210/time_trace.c",
        "firmware/src/capabilities/time.c",
        "platforms/k210/capabilities/time_adapter.c",
    )]
    fixtures = {"CT-09": sha256(FIXTURE)}
    flags = ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror"]
    with tempfile.TemporaryDirectory(prefix="hackylens-ct09-") as temporary:
        executable = Path(temporary) / ("time_trace.exe" if os.name == "nt" else "time_trace")
        compile_trace(compiler, flags, executable, (include, capabilities, stubs), sources)
        run_cases(executable, cases, fixture_args, trace_dir)
        if not time_only:
            runtime_executable = executable.with_stem("lifecycle_trace")
            includes = (include, capabilities, ROOT / "firmware/src", RUNTIME_SOURCE.parent,
                        ROOT / "sdk/include", ROOT / "tests")
            runtime_inputs = {name: source_overrides.get(ROOT / name, ROOT / name) for name in LIFECYCLE_SOURCES}
            compile_trace(compiler, flags, runtime_executable, includes, runtime_inputs.values())
            lifecycle = json.loads(LIFECYCLE_FIXTURE.read_text(encoding="utf-8"))
            run_cases(runtime_executable, lifecycle, lambda case: case["actions"], trace_dir)
            fixtures["lifecycle"] = sha256(LIFECYCLE_FIXTURE)
            inputs.update(runtime_inputs)
            inputs.update({name: ROOT / name for name in (
                "firmware/src/app_runtime/runtime_private.h", "firmware/src/app_runtime/switch.h",
                "sdk/include/hackylens/app/context.h", "sdk/include/hackylens/app/runtime.h",
                "tests/app_runtime_host_support.h",
            )})
            lights_executable = executable.with_stem("lights_trace")
            lights_sources = (
                "conformance/adapters/k210/lights_trace.c", "tests/lights_normative_k210_backend.c",
                "firmware/src/capabilities/lights.c", "platforms/k210/capabilities/lights_adapter.c",
            )
            compile_trace(compiler, flags, lights_executable, (include, capabilities, ROOT / "tests"),
                          [ROOT / source for source in lights_sources])
            lights_cases = json.loads(LIGHTS_FIXTURE.read_text(encoding="utf-8"))
            run_cases(lights_executable, lights_cases, lambda case: case["actions"], trace_dir)
            fixtures["CT-11"] = sha256(LIGHTS_FIXTURE)
            inputs.update({source: ROOT / source for source in lights_sources})
            link_executable = executable.with_stem("external_link_trace")
            link_sources = (
                "conformance/adapters/k210/external_link_trace.c", "tests/k210_external_link_harness.c",
                "tests/external_link_normative_suite.c", "firmware/src/capabilities/external_link.c",
                LINK_PROVIDER_SOURCE.relative_to(ROOT).as_posix(),
            )
            link_inputs = {name: source_overrides.get(ROOT / name, ROOT / name) for name in link_sources}
            compile_trace(compiler, [*flags, "-DHK_K210_EXTERNAL_LINK_TRACE"], link_executable,
                          (include, capabilities, ROOT / "tests", ROOT / "platforms/k210/hal"), link_inputs.values())
            run_cases(link_executable, json.loads(LINK_FIXTURE.read_text(encoding="utf-8")),
                      lambda case: case["actions"], trace_dir)
            fixtures["external_link"] = sha256(LINK_FIXTURE)
            inputs.update(link_inputs)
            inputs.update({name: ROOT / name for name in (
                "firmware/include/hackylens/capability/external_link.h",
                "firmware/src/capabilities/external_link_provider.h", "platforms/k210/hal/hal_external_link.h",
                "tests/k210_external_link_peer.h", "tests/external_link_normative_suite.h",
            )})
            script_executable = executable.with_stem("shared_script_trace")
            script_sources = (
                "conformance/adapters/k210/shared_script_trace.c",
                "tests/micropython_bindings_harness.c",
                SCRIPT_BRIDGE_SOURCE.relative_to(ROOT).as_posix(),
                "firmware/src/core/shared_buffer.c", "firmware/src/services/vision_result_service.c",
                "tests/hal_shared_buffer_fake.c", *lights_sources[1:],
            )
            script_inputs = {name: source_overrides.get(ROOT / name, ROOT / name) for name in script_sources}
            script_flags = [*flags, "-DMICROPYTHON_BINDING_TESTING", "-DHK_K210_SHARED_SCRIPT_TRACE",
                            "-DHK_ENABLE_AUDIO_FEATURE=1"]
            compile_trace(compiler, script_flags, script_executable,
                          (include, capabilities, ROOT / "tests", SCRIPT_BRIDGE_SOURCE.parent,
                           ROOT / "firmware/src/services", ROOT / "firmware/src/drivers",
                           ROOT / "firmware/src/config", ROOT / "firmware/assets", ROOT / "platforms/k210/hal"),
                          script_inputs.values())
            run_cases(script_executable, json.loads(SCRIPT_FIXTURE.read_text(encoding="utf-8")),
                      lambda case: case["actions"] * case.get("repeat", 1), trace_dir)
            fixtures["shared_script"] = sha256(SCRIPT_FIXTURE)
            inputs.update(script_inputs)
            inputs.update({name: ROOT / name for name in (
                "firmware/src/adapters/micropython/micropython_capability_bridge.h",
                "tests/k210_shared_script_fixture.h", "tests/micropython_binding_test_platform.h",
                "tests/lights_normative_backend.h",
            )})
            vm_stub = ROOT / "tests/fixtures/micropython_runtime_stub"
            script_runtime_executable = executable.with_stem("script_runtime_trace")
            script_runtime_sources = (
                "conformance/adapters/k210/script_runtime_trace.c", "tests/micropython_runtime_harness.c",
                "firmware/src/services/micropython_runtime.c", "firmware/src/core/shared_buffer.c",
                "tests/hal_shared_buffer_fake.c",
            )
            script_runtime_inputs = {name: ROOT / name for name in script_runtime_sources}
            (executable.parent / "hk_config.h").write_text("#define HK_MICROPYTHON_WDT_FAULT_INJECTION 0\n",
                                                           encoding="utf-8", newline="\n")
            compile_trace(compiler, [*flags, "-DHK_K210_SCRIPT_RUNTIME_TRACE"], script_runtime_executable,
                          (include, ROOT / "sdk/include", ROOT / "tests", ROOT / "firmware/src/services",
                           ROOT / "platforms/k210/hal", vm_stub, executable.parent), script_runtime_inputs.values())
            run_cases(script_runtime_executable, json.loads(SCRIPT_RUNTIME_FIXTURE.read_text(encoding="utf-8")),
                      lambda case: case["actions"], trace_dir)
            fixtures["script_runtime"] = sha256(SCRIPT_RUNTIME_FIXTURE)
            inputs.update(script_runtime_inputs)
            inputs.update({name: ROOT / name for name in (
                "firmware/src/services/micropython_runtime.h", "firmware/src/services/core1_executor.h",
                "tests/k210_script_runtime_fixture.h",
            )})
            inputs.update({path.relative_to(ROOT).as_posix(): path for path in vm_stub.rglob("*.h")})
    if not time_only:
        inputs.update(composition_trace.collect(trace_dir, compiler, flags))
        fixtures["CT-02"] = sha256(composition_trace.FIXTURE)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    compiler_version = subprocess.check_output([compiler, "--version"], text=True).splitlines()[0]
    compared_sources = [name for name in inputs if not name.startswith("conformance/")]
    changed = subprocess.check_output(["git", "diff", "--name-only", "HEAD", "--", *compared_sources],
                                      cwd=ROOT, text=True).splitlines()
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "--",
                                        *compared_sources], cwd=ROOT, text=True).splitlines()
    changed = sorted(set(changed + untracked))
    source_variant = "working-tree" if any(not name.startswith("tests/") for name in changed) else "baseline"
    substituted = {name for name in compared_sources if inputs[name] != ROOT / name}
    if substituted:
        source_variant = "mutant"
        changed = sorted(set(changed) | substituted)
    scopes = ["Core"]
    bindings = {"Time": {"version": "0.1", "features": ["monotonic_us", "sleep_until"]}}
    limits = {"Time": TIME_LIMITS}
    if not time_only:
        scopes.extend(("Lights", "External Link", "Shared Script"))
        bindings["Lights"] = {"version": "0.1", "features": ["backlight", "illumination", "rgb"]}
        bindings["External Link"] = {"version": "0.1", "features": ["uart", "i2c_controller", "i2c_target"]}
        limits["Lights"] = {"channel_mask": 7, "max_level": 1000}
        limits["External Link"] = {"poll_bytes": 32, "i2c_transfer_bytes": 256, "active_operations": 1}
        limits["Shared Script"] = {"observed_pending_requests": 1}
    manifest = {
        "schema_version": 1,
        "specification": "0.1.0",
        "implementation": "HackyLens K210",
        "implementation_version": version,
        "implementation_revision": revision,
        "source_variant": source_variant,
        "changed_inputs": changed,
        "evaluated_scopes": scopes,
        "environment": {"kind": "host-controlled-clock", "platform": platform.platform(),
                        "compiler": compiler_version, "flags": flags},
        "configuration": ("K210 Time binding with host clock, wait and lock hooks" if time_only
                          else "K210 Time binding with host hooks, native runtime with simulated "
                               "providers, Lights and External Link with peripheral hooks, the MicroPython "
                               "capability bridge with a controlled scheduler, MicroPython runtime with a VM "
                               "stub and controlled executor, and SEN0305 composition fixtures"),
        "bindings": bindings,
        "limits": limits,
        "fixtures_sha256": fixtures,
        "input_sha256": {name: sha256(path) for name, path in inputs.items()},
    }
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                               encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    collect(args.destination)


if __name__ == "__main__":
    main()
