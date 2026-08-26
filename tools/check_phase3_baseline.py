#!/usr/bin/env python3
"""Validate and reproduce the Phase 3 baseline pinned to Phase 2 closure."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from typing import Any

import check_phase2_resources


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASELINE = ROOT / "docs" / "evidence" / "phase3-baseline.json"
PINNED_BASELINE_SHA256 = (
    "5769451c8622381e8740cc091f21ba3fed2448a755b77911a5a2891b8ebccece"
)
SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
DISPATCH_RESULT_RE = re.compile(
    r"PHASE3_DISPATCH_BASELINE_OK host_p99_ns=(\d+) limit_us=100 "
    r"samples=101 iterations=1000"
)

ROOT_FIELDS = {"baseline", "budgets", "formulas", "schema"}
BASELINE_FIELDS = {
    "board", "closure", "dispatch", "firmware_version", "profiles", "target",
    "toolchain",
}
CLOSURE_FIELDS = {
    "automated_result", "closure_commit", "closure_result",
    "implementation_commit",
}
ARTIFACT_REF_FIELDS = {"path", "sha256"}
DISPATCH_FIELDS = {
    "harness_path", "harness_sha256", "iterations_per_sample",
    "measurement_scope", "observed_host_p99_ns", "recorded_runs",
    "samples_per_run", "source_path", "source_sha256",
}
PROFILE_FIELDS = {
    "attestation_sha256", "build_profile", "capabilities_sha256",
    "composition_sha256", "disabled_apps", "elf", "image",
    "release_qualified",
}
ELF_FIELDS = {
    "bss_bytes", "data_bytes", "sha256", "static_ram_bytes", "text_bytes",
}
IMAGE_FIELDS = {"flash_occupied_bytes", "raw_bytes", "sha256"}
TOOLCHAIN_FIELDS = {
    "archive_sha256", "kendryte_standalone_sdk_revision", "kendryte_toolchain",
}
BUDGET_FIELDS = {
    "additional_full_framebuffers_max",
    "erase_rounded_flash_delta_max_bytes",
    "hardware_dispatch_p99_max_us",
    "host_dispatch_p99_delta_max_us",
    "host_dispatch_p99_max_us",
    "new_background_tasks_max",
    "new_general_queues_max",
    "new_heap_allocations_max",
    "new_runtime_cores_max",
    "runtime_stack_frame_max_bytes",
    "static_ram_delta_max_bytes",
}
FORMULA_FIELDS = {"dispatch_delta", "flash_occupied", "static_ram"}
EXPECTED_BUDGETS = {
    "additional_full_framebuffers_max": 0,
    "erase_rounded_flash_delta_max_bytes": 65536,
    "hardware_dispatch_p99_max_us": 100,
    "host_dispatch_p99_delta_max_us": 50,
    "host_dispatch_p99_max_us": 100,
    "new_background_tasks_max": 0,
    "new_general_queues_max": 0,
    "new_heap_allocations_max": 0,
    "new_runtime_cores_max": 0,
    "runtime_stack_frame_max_bytes": 32768,
    "static_ram_delta_max_bytes": 16384,
}
EXPECTED_FORMULAS = {
    "dispatch_delta":
        "measured_host_p99_us - baseline_observed_host_p99_us",
    "flash_occupied":
        "ceil((raw_image_bytes + 37) / erase_size) * erase_size",
    "static_ram": "data_bytes + bss_bytes",
}


def canonical_json_bytes(document: Any) -> bytes:
    return (json.dumps(
        document, ensure_ascii=False, indent=2, sort_keys=True,
        separators=(",", ": "),
    ) + "\n").encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def normalized_text_sha256(value: bytes) -> str:
    text = value.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")
    return sha256_bytes(text.encode("utf-8"))


def exact_fields(value: Any, expected: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        raise RuntimeError(f"{label} has missing or unknown fields")
    return value


def string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"{label} must be a non-empty string")
    return value


def integer(value: Any, label: str, *, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise RuntimeError(f"{label} must be an integer >= {minimum}")
    return value


def digest(value: Any, label: str) -> str:
    result = string(value, label)
    if SHA256_RE.fullmatch(result) is None:
        raise RuntimeError(f"{label} must be lowercase SHA-256")
    return result


def validate_artifact_ref(value: Any, label: str) -> dict[str, Any]:
    result = exact_fields(value, ARTIFACT_REF_FIELDS, label)
    string(result["path"], f"{label}.path")
    digest(result["sha256"], f"{label}.sha256")
    return result


def validate_profile(value: Any, label: str) -> dict[str, Any]:
    profile = exact_fields(value, PROFILE_FIELDS, label)
    string(profile["build_profile"], f"{label}.build_profile")
    for field in (
        "attestation_sha256", "capabilities_sha256", "composition_sha256",
    ):
        digest(profile[field], f"{label}.{field}")
    if not isinstance(profile["release_qualified"], bool):
        raise RuntimeError(f"{label}.release_qualified must be boolean")
    disabled = profile["disabled_apps"]
    if (not isinstance(disabled, list) or
            any(not isinstance(item, str) for item in disabled) or
            disabled != sorted(set(disabled))):
        raise RuntimeError(f"{label}.disabled_apps must be sorted unique strings")

    elf = exact_fields(profile["elf"], ELF_FIELDS, f"{label}.elf")
    image = exact_fields(profile["image"], IMAGE_FIELDS, f"{label}.image")
    for field in ELF_FIELDS - {"sha256"}:
        integer(elf[field], f"{label}.elf.{field}", minimum=1)
    digest(elf["sha256"], f"{label}.elf.sha256")
    if elf["static_ram_bytes"] != elf["data_bytes"] + elf["bss_bytes"]:
        raise RuntimeError(f"{label}.elf static RAM formula mismatch")
    for field in IMAGE_FIELDS - {"sha256"}:
        integer(image[field], f"{label}.image.{field}", minimum=1)
    digest(image["sha256"], f"{label}.image.sha256")
    return profile


def validate_document(document: Any) -> dict[str, Any]:
    root = exact_fields(document, ROOT_FIELDS, "root")
    if type(root["schema"]) is not int or root["schema"] != 1:
        raise RuntimeError("schema must be integer 1")
    baseline = exact_fields(root["baseline"], BASELINE_FIELDS, "baseline")
    if baseline["board"] != "huskylens-sen0305":
        raise RuntimeError("Phase 3 baseline board must be huskylens-sen0305")
    if baseline["firmware_version"] != "0.4.0" or baseline["target"] != "full":
        raise RuntimeError("Phase 3 must start from exact Phase 2 firmware/target")

    closure = exact_fields(baseline["closure"], CLOSURE_FIELDS, "baseline.closure")
    for field in ("closure_commit", "implementation_commit"):
        if SHA1_RE.fullmatch(string(closure[field], f"baseline.closure.{field}")) is None:
            raise RuntimeError(f"baseline.closure.{field} must be Git SHA-1")
    validate_artifact_ref(closure["automated_result"], "baseline.closure.automated_result")
    validate_artifact_ref(closure["closure_result"], "baseline.closure.closure_result")

    dispatch = exact_fields(baseline["dispatch"], DISPATCH_FIELDS, "baseline.dispatch")
    for field in ("harness_path", "measurement_scope", "source_path"):
        string(dispatch[field], f"baseline.dispatch.{field}")
    for field in ("harness_sha256", "source_sha256"):
        digest(dispatch[field], f"baseline.dispatch.{field}")
    for field in (
        "iterations_per_sample", "observed_host_p99_ns", "recorded_runs",
        "samples_per_run",
    ):
        integer(dispatch[field], f"baseline.dispatch.{field}", minimum=1)
    if (dispatch["iterations_per_sample"] != 1000 or
            dispatch["samples_per_run"] != 101 or
            dispatch["recorded_runs"] != 10):
        raise RuntimeError("dispatch baseline sampling plan is not canonical")

    profiles = exact_fields(
        baseline["profiles"], {"full", "micropython-disabled"},
        "baseline.profiles",
    )
    full = validate_profile(profiles["full"], "baseline.profiles.full")
    disabled = validate_profile(
        profiles["micropython-disabled"],
        "baseline.profiles.micropython-disabled",
    )
    if full["disabled_apps"] or not full["release_qualified"]:
        raise RuntimeError("full baseline profile identity mismatch")
    if disabled["disabled_apps"] != ["micropython"] or disabled["release_qualified"]:
        raise RuntimeError("micropython-disabled baseline profile identity mismatch")

    toolchain = exact_fields(baseline["toolchain"], TOOLCHAIN_FIELDS, "baseline.toolchain")
    for field in TOOLCHAIN_FIELDS:
        string(toolchain[field], f"baseline.toolchain.{field}")
    digest(toolchain["archive_sha256"], "baseline.toolchain.archive_sha256")
    if root["budgets"] != EXPECTED_BUDGETS or set(root["budgets"]) != BUDGET_FIELDS:
        raise RuntimeError("Phase 3 numerical budgets are not canonical")
    if root["formulas"] != EXPECTED_FORMULAS or set(root["formulas"]) != FORMULA_FIELDS:
        raise RuntimeError("Phase 3 measurement formulas are not canonical")
    return root


def load_baseline(path: Path = DEFAULT_BASELINE) -> dict[str, Any]:
    try:
        encoded = path.read_bytes()
        document = json.loads(encoded.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"cannot read Phase 3 baseline: {exc}") from exc
    if encoded != canonical_json_bytes(document):
        raise RuntimeError("Phase 3 baseline is not canonical UTF-8 JSON")
    actual = sha256_bytes(encoded)
    if actual != PINNED_BASELINE_SHA256:
        raise RuntimeError(
            f"Phase 3 baseline digest mismatch: expected {PINNED_BASELINE_SHA256}, got {actual}"
        )
    return validate_document(document)


def git_file(commit: str, relative: str) -> bytes:
    result = subprocess.run(
        ["git", "show", f"{commit}:{relative}"], cwd=ROOT, check=True,
        capture_output=True,
    )
    return result.stdout


def verify_provenance(document: dict[str, Any]) -> None:
    baseline = document["baseline"]
    closure = baseline["closure"]
    commit = closure["closure_commit"]
    subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, "HEAD"],
        cwd=ROOT, check=True, capture_output=True,
    )
    if git_file(commit, "VERSION").decode("utf-8").strip() != "0.4.0":
        raise RuntimeError("Phase 2 closure VERSION is not 0.4.0")

    for field in ("closure_result", "automated_result"):
        artifact = closure[field]
        path = ROOT / artifact["path"]
        if sha256(path) != artifact["sha256"]:
            raise RuntimeError(f"{field} current digest differs from Phase 3 baseline")
    closure_document = json.loads(
        (ROOT / closure["closure_result"]["path"]).read_text(encoding="utf-8")
    )
    if closure_document.get("accepted") is not True:
        raise RuntimeError("Phase 2 closure is not accepted")
    if closure_document.get("implementation", {}).get("commit") != closure["implementation_commit"]:
        raise RuntimeError("Phase 2 implementation commit mismatch")
    if closure_document.get("automated", {}).get("sha256") != closure["automated_result"]["sha256"]:
        raise RuntimeError("Phase 2 closure automated-result hash mismatch")

    historical_closure = git_file(commit, closure["closure_result"]["path"])
    if sha256_bytes(historical_closure) != closure["closure_result"]["sha256"]:
        raise RuntimeError("closure commit does not contain the pinned closure result")
    dispatch = baseline["dispatch"]
    historical_source = git_file(commit, dispatch["source_path"])
    if normalized_text_sha256(historical_source) != dispatch["source_sha256"]:
        raise RuntimeError("dispatch source is not pinned to exact Phase 2 closure")
    harness = ROOT / dispatch["harness_path"]
    if normalized_text_sha256(harness.read_bytes()) != dispatch["harness_sha256"]:
        raise RuntimeError("Phase 3 dispatch baseline harness changed")

    automated = json.loads(
        (ROOT / closure["automated_result"]["path"]).read_text(encoding="utf-8")
    )
    for name, profile in baseline["profiles"].items():
        candidate = automated["builds"]["profiles"][name]
        for field in (
            "attestation_sha256", "build_profile", "capabilities_sha256",
            "composition_sha256", "disabled_apps", "elf", "image",
            "release_qualified",
        ):
            if profile[field] != candidate[field]:
                raise RuntimeError(f"{name} baseline differs from Phase 2 automated result")


def profile_budget_deltas(
    baseline_profile: dict[str, Any], measured: dict[str, Any],
    budgets: dict[str, int],
) -> tuple[int, int]:
    flash_delta = (
        measured["image"]["flash_occupied_bytes"] -
        baseline_profile["image"]["flash_occupied_bytes"]
    )
    static_delta = (
        measured["elf"]["static_ram_bytes"] -
        baseline_profile["elf"]["static_ram_bytes"]
    )
    if flash_delta > budgets["erase_rounded_flash_delta_max_bytes"]:
        raise RuntimeError(f"flash delta {flash_delta} exceeds Phase 3 budget")
    if static_delta > budgets["static_ram_delta_max_bytes"]:
        raise RuntimeError(f"static RAM delta {static_delta} exceeds Phase 3 budget")
    return flash_delta, static_delta


def verify_profile(document: dict[str, Any], profile_name: str) -> tuple[int, int]:
    paths = check_phase2_resources.artifact_paths()
    measured = check_phase2_resources.artifact_measurement(paths)
    composition = check_phase2_resources.read_json(paths["composition"], "composition")
    attestation = check_phase2_resources.read_json(paths["attestation"], "attestation")
    expected_disabled = [] if profile_name == "full" else ["micropython"]
    if composition.get("disabled_apps") != expected_disabled:
        raise RuntimeError(f"{profile_name} composition identity mismatch")
    expected_build_profile = (
        "hackylens-full" if profile_name == "full" else
        "hackylens-feature-modified"
    )
    if (attestation.get("board_id") != document["baseline"]["board"] or
            attestation.get("firmware_version") != document["baseline"]["firmware_version"] or
            attestation.get("build_profile") != expected_build_profile):
        raise RuntimeError(f"{profile_name} attestation identity mismatch")
    return profile_budget_deltas(
        document["baseline"]["profiles"][profile_name], measured,
        document["budgets"],
    )


def compiler() -> str:
    candidate = os.environ.get("CC")
    if candidate and Path(candidate).is_file():
        return candidate
    discovered = shutil.which("gcc") or shutil.which("cc")
    if not discovered:
        raise RuntimeError("host C compiler is required for dispatch reproduction")
    return discovered


def measure_dispatch(document: dict[str, Any]) -> int:
    dispatch = document["baseline"]["dispatch"]
    with tempfile.TemporaryDirectory(prefix="hackylens-phase3-dispatch-") as temp:
        executable = Path(temp) / (
            "phase3_dispatch.exe" if os.name == "nt" else "phase3_dispatch"
        )
        subprocess.run([
            compiler(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            f"-I{ROOT / 'firmware' / 'include'}",
            f"-I{ROOT / 'firmware' / 'src'}",
            str(ROOT / dispatch["harness_path"]),
            str(ROOT / dispatch["source_path"]),
            "-o", str(executable),
        ], cwd=ROOT, check=True)
        result = subprocess.run(
            [str(executable)], cwd=ROOT, check=True, capture_output=True,
            text=True, timeout=30,
        )
    match = DISPATCH_RESULT_RE.search(result.stdout)
    if match is None:
        raise RuntimeError(f"unexpected dispatch harness output: {result.stdout!r}")
    measured_ns = int(match.group(1))
    measured_us = measured_ns / 1000.0
    baseline_us = dispatch["observed_host_p99_ns"] / 1000.0
    budgets = document["budgets"]
    if measured_us > budgets["host_dispatch_p99_max_us"]:
        raise RuntimeError("host dispatch p99 exceeds Phase 3 absolute budget")
    if measured_us - baseline_us > budgets["host_dispatch_p99_delta_max_us"]:
        raise RuntimeError("host dispatch p99 delta exceeds Phase 3 budget")
    return measured_ns


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument(
        "--verify-profile", choices=("full", "micropython-disabled")
    )
    parser.add_argument("--measure-dispatch", action="store_true")
    args = parser.parse_args()
    try:
        document = load_baseline(args.baseline)
        verify_provenance(document)
        details: list[str] = []
        if args.verify_profile:
            flash, static = verify_profile(document, args.verify_profile)
            details.append(
                f"{args.verify_profile} flash_delta={flash} static_ram_delta={static}"
            )
        if args.measure_dispatch:
            details.append(f"host_dispatch_p99_ns={measure_dispatch(document)}")
    except (RuntimeError, OSError, KeyError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"[FAIL] Phase 3 baseline: {exc}")
        return 1
    suffix = f"; {'; '.join(details)}" if details else ""
    print(f"[OK] Phase 3 baseline is canonical and provenance-checked{suffix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
