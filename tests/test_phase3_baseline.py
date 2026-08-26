from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "check_phase3_baseline.py"
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

spec = importlib.util.spec_from_file_location("check_phase3_baseline", TOOL_PATH)
assert spec is not None and spec.loader is not None
CHECKER = importlib.util.module_from_spec(spec)
spec.loader.exec_module(CHECKER)


class Phase3BaselineTests(unittest.TestCase):
    def test_canonical_baseline_keeps_lf_on_windows_checkouts(self) -> None:
        result = subprocess.run(
            [
                "git", "check-attr", "eol", "--",
                "docs/evidence/phase3-baseline.json",
            ],
            cwd=ROOT,
            check=True,
            text=True,
            capture_output=True,
        )
        self.assertEqual(
            result.stdout.strip(),
            "docs/evidence/phase3-baseline.json: eol: lf",
        )

    def test_repository_baseline_is_canonical_and_pins_phase2_closure(self) -> None:
        document = CHECKER.load_baseline()
        CHECKER.verify_provenance(document)
        closure = document["baseline"]["closure"]
        self.assertEqual(
            closure["closure_commit"],
            "ed2adcebb757ccf4c8bdaf5b7ba3f0b9c596eedb",
        )
        self.assertEqual(
            closure["implementation_commit"],
            "f8b76f441d25a7be02bf8c804736750306b8f2b7",
        )

    def test_numerical_budgets_are_strict(self) -> None:
        document = CHECKER.load_baseline()
        changed = copy.deepcopy(document)
        changed["budgets"]["static_ram_delta_max_bytes"] += 1
        with self.assertRaisesRegex(RuntimeError, "budgets are not canonical"):
            CHECKER.validate_document(changed)

    def test_flash_and_static_ram_boundaries_are_enforced(self) -> None:
        document = CHECKER.load_baseline()
        baseline = document["baseline"]["profiles"]["full"]
        measured = copy.deepcopy(baseline)
        measured["image"]["flash_occupied_bytes"] += 65536
        measured["elf"]["static_ram_bytes"] += 16384
        self.assertEqual(
            CHECKER.profile_budget_deltas(
                baseline, measured, document["budgets"]
            ),
            (65536, 16384),
        )
        measured["image"]["flash_occupied_bytes"] += 4096
        with self.assertRaisesRegex(RuntimeError, "flash delta"):
            CHECKER.profile_budget_deltas(
                baseline, measured, document["budgets"]
            )
        measured = copy.deepcopy(baseline)
        measured["elf"]["static_ram_bytes"] += 16385
        with self.assertRaisesRegex(RuntimeError, "static RAM delta"):
            CHECKER.profile_budget_deltas(
                baseline, measured, document["budgets"]
            )

    def test_dispatch_baseline_is_reproducible(self) -> None:
        document = CHECKER.load_baseline()
        measured_ns = CHECKER.measure_dispatch(document)
        self.assertLessEqual(
            measured_ns,
            document["budgets"]["host_dispatch_p99_max_us"] * 1000,
        )


if __name__ == "__main__":
    unittest.main()
