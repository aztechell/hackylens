from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"


def load_tool(name: str):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Phase2QualificationTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise RuntimeError("host C compiler is required")
        return compiler

    def test_registry_validation_host_p99(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-phase2-latency-") as temp:
            executable = Path(temp) / (
                "phase2_latency.exe" if os.name == "nt" else "phase2_latency"
            )
            subprocess.run([
                self.compiler(), "-std=c11", "-O2", "-Wall", "-Wextra",
                "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'tests'}",
                str(ROOT / "tests" / "phase2_registry_latency_harness.c"),
                str(ROOT / "tests" / "capability_fake_provider.c"),
                str(ROOT / "firmware" / "src" / "capabilities" /
                    "capability_core.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT, text=True,
                capture_output=True, timeout=30,
            )
        match = re.search(r"host_p99_ns=(\d+) limit_us=100", result.stdout)
        self.assertIsNotNone(match, result.stdout)
        self.assertLessEqual(int(match.group(1)), 100_000)

    def test_contract_matrix_is_exactly_fake_and_k210_for_five_capabilities(self) -> None:
        contracts = load_tool("run_phase2_contracts")
        matrix = contracts.CONTRACT_MATRIX
        self.assertEqual(
            sorted({item["capability"] for item in matrix}),
            ["display", "external-link", "input", "lights", "time"],
        )
        for capability in {item["capability"] for item in matrix}:
            self.assertEqual(
                [item["backend"] for item in matrix
                 if item["capability"] == capability],
                ["fake", "k210"],
            )
        first = contracts.matrix_document(passed=True)
        second = contracts.matrix_document(passed=True)
        self.assertEqual(first, second)
        self.assertRegex(first["manifest_sha256"], r"^[0-9a-f]{64}$")

    def test_result_rejects_physical_claims_and_stale_source(self) -> None:
        resources = load_tool("check_phase2_resources")
        exit_gate = load_tool("check_phase2_exit")
        contracts = load_tool("run_phase2_contracts")
        expected_compositions = resources.expected_diagnostic_compositions()
        result = {
            "schema": 1,
            "phase": "2.12",
            "gate": "pre-hardware",
            "accepted": True,
            "source": resources.repository_source_identity(),
            "contracts": contracts.matrix_document(passed=True),
            "composition": {
                "capability_ids": list(resources.EXPECTED_CAPABILITIES),
                "diagnostic_expectations": expected_compositions,
            },
            "builds": {
                "profiles": {"full": {}, "micropython-disabled": {}},
                "capability_absent_diagnostics": {
                    capability: {} for capability in resources.EXPECTED_CAPABILITIES
                },
                "cube_conformance": {
                    "compile_only": True, "runtime_supported": False,
                },
            },
            "resources": {"accepted": True},
            "latency": {
                "accepted": True,
                "elapsed_hardware_measurements": {
                    "registry_validation_sen0305_p99":
                        "deferred-to-phase-2.13",
                },
            },
            "physical_scope": {
                "claims": [], "deferred_to": "Phase 2.13", "status": "not-run",
            },
        }
        exit_gate.verify_result(result, verify_receipts=False)
        claimed = dict(result)
        claimed["hardware_qualified"] = True
        with self.assertRaisesRegex(RuntimeError, "missing or unknown"):
            resources.validate_result_document(claimed)
        stale = dict(result)
        stale["source"] = {**result["source"], "manifest_sha256": "0" * 64}
        with self.assertRaisesRegex(RuntimeError, "source identity"):
            exit_gate.verify_result(stale, verify_receipts=False)

    def test_exit_workflow_requires_profile_capture_after_baseline_checks(self) -> None:
        exit_gate = load_tool("check_phase2_exit")
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )
        # The current workflow is updated by Phase 2.12. This assertion also
        # acts as a negative fixture against moving a capture before its build.
        if "check_phase2_exit.py --mode pre-hardware" in workflow:
            exit_gate.verify_workflow(workflow)
            broken = workflow.replace(
                "Invoke-NativeChecked python tools/check_phase2_resources.py --capture-profile full",
                "",
                1,
            )
            with self.assertRaisesRegex(RuntimeError, "omits Phase 2 gate"):
                exit_gate.verify_workflow(broken)


if __name__ == "__main__":
    unittest.main()
