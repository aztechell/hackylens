#!/usr/bin/env python3
"""Run the Phase 2 fake/K210 adapter contract matrix as one strict suite."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
TEST_ROOT = ROOT / "tests"

CONTRACT_MATRIX: tuple[dict[str, str], ...] = (
    {
        "capability": "time",
        "backend": "fake",
        "test": "test_time_capability.TimeCapabilityTests.test_time_contract_and_bounded_object",
    },
    {
        "capability": "time",
        "backend": "k210",
        "test": "test_time_capability.TimeCapabilityTests.test_k210_any_core_clock_read_is_inside_provider_lock",
    },
    {
        "capability": "input",
        "backend": "fake",
        "test": "test_input_capability.InputCapabilityTests.test_debounce_events_cursors_and_overflow",
    },
    {
        "capability": "input",
        "backend": "k210",
        "test": "test_input_capability.InputCapabilityTests.test_k210_raw_sampler_is_gated_to_ten_milliseconds",
    },
    {
        "capability": "display",
        "backend": "fake",
        "test": "test_display_contract.DisplayContractTests.test_fake_proves_display_state_machine",
    },
    {
        "capability": "display",
        "backend": "k210",
        "test": "test_micropython_bindings.MicroPythonBindingSafetyTests.test_k210_display_adapter_composition_cancel_and_cleanup",
    },
    {
        "capability": "external-link",
        "backend": "fake",
        "test": "test_external_link_contract.ExternalLinkContractTests.test_fake_proves_external_link_state_machine",
    },
    {
        "capability": "external-link",
        "backend": "k210",
        "test": "test_external_link_contract.ExternalLinkContractTests.test_k210_adapter_passes_the_same_normative_suite",
    },
    {
        "capability": "lights",
        "backend": "fake",
        "test": "test_lights_capability.LightsCapabilityTests.test_masks_deadlines_cleanup_and_bounded_object",
    },
    {
        "capability": "lights",
        "backend": "k210",
        "test": "test_lights_capability.LightsCapabilityTests.test_k210_adapter_converts_only_after_cancel_and_deadline_checks",
    },
)

SUPPLEMENTAL_TESTS: tuple[str, ...] = (
    "test_capability_core.CapabilityCoreHostTests.test_common_lifecycle_contract_suite",
    "test_phase2_qualification.Phase2QualificationTests.test_registry_validation_host_p99",
    "test_pong.PongHostTests.test_fixed_step_physics_and_dirty_rendering",
)


def canonical_json_bytes(value: Any) -> bytes:
    return (json.dumps(
        value, ensure_ascii=False, allow_nan=False, indent=2, sort_keys=True,
        separators=(",", ": "),
    ) + "\n").encode("utf-8")


def matrix_document(*, passed: bool) -> dict[str, Any]:
    manifest = {
        "schema": 1,
        "suite": "phase2-adapter-contracts-v1",
        "matrix": [dict(item) for item in CONTRACT_MATRIX],
        "supplemental_tests": list(SUPPLEMENTAL_TESTS),
    }
    return {
        **manifest,
        "manifest_sha256": hashlib.sha256(
            canonical_json_bytes(manifest)
        ).hexdigest(),
        "passed": passed,
    }


def run_suite() -> None:
    selectors = [item["test"] for item in CONTRACT_MATRIX]
    selectors.extend(SUPPLEMENTAL_TESTS)
    subprocess.run(
        [sys.executable, "-m", "unittest", "-v", *selectors],
        cwd=TEST_ROOT,
        check=True,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write-receipt", type=Path)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args(argv)
    if args.list:
        print(canonical_json_bytes(matrix_document(passed=False)).decode(), end="")
        return 0
    try:
        run_suite()
    except subprocess.CalledProcessError as exc:
        print(
            f"[ERR] Phase 2 contract suite failed with exit code {exc.returncode}",
            file=sys.stderr,
        )
        return exc.returncode or 1
    document = matrix_document(passed=True)
    if args.write_receipt:
        args.write_receipt.parent.mkdir(parents=True, exist_ok=True)
        args.write_receipt.write_bytes(canonical_json_bytes(document))
    print(
        "[OK] Phase 2 adapter contract suite passed: "
        "5 capabilities x fake/K210 plus lifecycle, p99 and Pong gates"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
