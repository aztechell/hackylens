import copy
import json
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import check_resources as gate


class FirmwareResourceTests(unittest.TestCase):
    def test_budgets_accept_boundary_and_reject_each_excess(self):
        baseline = json.loads(gate.BASELINE.read_text(encoding="utf-8"))
        current = copy.deepcopy(baseline["measurement"])
        current["elf"]["static_ram_bytes"] += baseline["limits"]["static_ram_growth_bytes"]
        self.assertEqual(gate.budget_failures(current, baseline), [])
        current["elf"]["static_ram_bytes"] += 1
        self.assertEqual(len(gate.budget_failures(current, baseline)), 1)
        current["image"]["raw_bytes"] += 1
        self.assertEqual(len(gate.budget_failures(current, baseline)), 2)

    def test_no_new_direct_runtime_resource_sites(self):
        baseline = json.loads(gate.BASELINE.read_text(encoding="utf-8"))
        self.assertEqual(gate.resources._new_direct_resources(baseline["commit"]), [])
