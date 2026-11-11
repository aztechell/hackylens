import copy
import json
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import check_resources as gate


class FirmwareResourceTests(unittest.TestCase):
    def test_size_deltas_report_growth_and_savings(self):
        baseline = json.loads(gate.BASELINE.read_text(encoding="utf-8"))
        current = copy.deepcopy(baseline["measurement"])
        current["elf"]["static_ram_bytes"] -= 1024
        current["image"]["raw_bytes"] += 24576
        self.assertEqual(gate.size_deltas(current, baseline["measurement"]),
                         {"static_ram_bytes": -1024, "raw_image_bytes": 24576})

    def test_no_new_direct_runtime_resource_sites(self):
        baseline = json.loads(gate.BASELINE.read_text(encoding="utf-8"))
        self.assertEqual(gate.resources._new_direct_resources(baseline["commit"]), [])
