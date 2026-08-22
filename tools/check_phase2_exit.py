#!/usr/bin/env python3
"""Enforce the Phase 2.12 machine-checkable pre-hardware exit gate."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import check_phase2_evidence
import check_phase2_resources
import run_phase2_contracts


DEFAULT_RESULT = ROOT / "docs" / "evidence" / "phase2-result.json"


def verify_workflow(workflow: str) -> None:
    required = (
        "python tools/run_phase2_contracts.py --write-receipt build/phase2-qualification/contracts.json",
        "python tools/check_phase2_resources.py --capture-diagnostic",
        "python tools/build_firmware.py conformance --board sipeed-maix-cube",
        "python tools/check_phase2_resources.py --capture-conformance",
        "python tools/check_phase2_resources.py --capture-profile micropython-disabled",
        "python tools/check_phase2_resources.py --capture-profile full",
        "python tools/check_phase2_resources.py --check-result",
        "python tools/check_phase2_exit.py --mode pre-hardware",
    )
    for command in required:
        if command not in workflow:
            raise RuntimeError(f"release workflow omits Phase 2 gate: {command}")
    disabled_build = workflow.index(
        "build_firmware.py full --board huskylens-sen0305 --disable-app micropython"
    )
    disabled_baseline = workflow.index(
        "check_phase2_evidence.py --verify-profile micropython-disabled"
    )
    disabled_capture = workflow.index(
        "check_phase2_resources.py --capture-profile micropython-disabled"
    )
    full_build = workflow.index(
        "build_firmware.py full --board huskylens-sen0305\n"
    )
    full_baseline = workflow.index(
        "check_phase2_evidence.py --verify-profile full"
    )
    full_capture = workflow.index(
        "check_phase2_resources.py --capture-profile full"
    )
    final_resource = workflow.index("check_phase2_resources.py --check-result")
    final_exit = workflow.index("check_phase2_exit.py --mode pre-hardware")
    if not (
        disabled_build < disabled_baseline < disabled_capture
        and full_build < full_baseline < full_capture
        and full_capture < final_resource < final_exit
    ):
        raise RuntimeError("Phase 2 build capture/exit ordering is unsafe")


def verify_result(
    result: dict[str, Any], *, verify_receipts: bool = True
) -> None:
    check_phase2_resources.validate_result_document(result)
    if result["source"] != check_phase2_resources.repository_source_identity():
        raise RuntimeError("Phase 2 result source identity is stale")
    if result["composition"]["capability_ids"] != list(
        check_phase2_resources.EXPECTED_CAPABILITIES
    ):
        raise RuntimeError("Phase 2 result capability set is not exactly five")
    expected_compositions = (
        check_phase2_resources.expected_diagnostic_compositions()
    )
    if result["composition"]["diagnostic_expectations"] != expected_compositions:
        raise RuntimeError("Phase 2 result diagnostic composition is stale")
    expected_contracts = run_phase2_contracts.matrix_document(passed=True)
    if result["contracts"] != expected_contracts:
        raise RuntimeError("Phase 2 result contract matrix is stale")
    profiles = result["builds"].get("profiles")
    if not isinstance(profiles, dict) or set(profiles) != {
        "full", "micropython-disabled",
    }:
        raise RuntimeError("Phase 2 result firmware profile matrix is incomplete")
    diagnostics = result["builds"].get("capability_absent_diagnostics")
    if not isinstance(diagnostics, dict) or set(diagnostics) != set(
        check_phase2_resources.EXPECTED_CAPABILITIES
    ):
        raise RuntimeError("Phase 2 result diagnostic matrix is incomplete")
    cube = result["builds"].get("cube_conformance", {})
    if cube.get("compile_only") is not True or \
            cube.get("runtime_supported") is not False:
        raise RuntimeError("Cube is not represented as compile-conformance-only")
    if result["resources"].get("accepted") is not True:
        raise RuntimeError("Phase 2 result resource gate failed")
    if result["latency"].get("accepted") is not True:
        raise RuntimeError("Phase 2 result latency gate failed")
    elapsed = result["latency"].get("elapsed_hardware_measurements", {})
    if not elapsed or any(
        value != "deferred-to-phase-2.13" for value in elapsed.values()
    ):
        raise RuntimeError("physical latency deferral is missing or overclaimed")
    if verify_receipts:
        baseline = check_phase2_evidence.load_baseline()
        check_phase2_evidence.verify_provenance(baseline)
        current = check_phase2_resources.build_result(baseline)
        if result != current:
            raise RuntimeError("Phase 2 result does not match build receipts")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", required=True, choices=("pre-hardware",))
    parser.add_argument("--result", type=Path, default=DEFAULT_RESULT)
    parser.add_argument(
        "--without-build-receipts", action="store_true",
        help="Validate tracked source/schema only (tests and diagnostics).",
    )
    args = parser.parse_args(argv)
    try:
        result = check_phase2_resources.read_canonical_json(
            args.result, "Phase 2 rolling result"
        )
        verify_result(result, verify_receipts=not args.without_build_receipts)
        workflow = (ROOT / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )
        verify_workflow(workflow)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError,
            json.JSONDecodeError) as exc:
        print(f"[ERR] Phase 2 exit gate failed: {exc}", file=sys.stderr)
        return 1
    print(
        "[OK] Phase 2.12 pre-hardware exit passed; physical SEN0305 "
        "acceptance remains Phase 2.13"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
