from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))


def load_checker():
    path = TOOLS / "check_phase2_evidence.py"
    spec = importlib.util.spec_from_file_location("hackylens_phase2_evidence", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


CHECKER = load_checker()


class Phase2EvidenceTest(unittest.TestCase):
    def test_profile_budget_accepts_limits_and_rejects_overages(self) -> None:
        document = CHECKER.load_baseline()
        profile = document["baseline"]["profiles"]["full"]
        acceptance = document["acceptance"]
        erase_size = 4096
        baseline_occupied = profile["image"]["flash_occupied_bytes"]
        allowed_occupied = baseline_occupied + acceptance["flash_delta_max_bytes"]
        allowed_raw = allowed_occupied - CHECKER.build_firmware.K210_IMAGE_OVERHEAD
        baseline_static = profile["elf"]["static_ram_bytes"]
        data_bytes = profile["elf"]["data_bytes"]
        allowed_bss = baseline_static + acceptance["static_ram_delta_max_bytes"] - data_bytes

        self.assertEqual(
            CHECKER.verify_profile_budget(
                document,
                "full",
                raw_bytes=allowed_raw,
                data_bytes=data_bytes,
                bss_bytes=allowed_bss,
                erase_size=erase_size,
            ),
            (acceptance["flash_delta_max_bytes"],
             acceptance["static_ram_delta_max_bytes"]),
        )
        with self.assertRaisesRegex(RuntimeError, "flash delta"):
            CHECKER.verify_profile_budget(
                document,
                "full",
                raw_bytes=allowed_raw + erase_size,
                data_bytes=data_bytes,
                bss_bytes=allowed_bss,
                erase_size=erase_size,
            )
        with self.assertRaisesRegex(RuntimeError, "static RAM delta"):
            CHECKER.verify_profile_budget(
                document,
                "full",
                raw_bytes=allowed_raw,
                data_bytes=data_bytes,
                bss_bytes=allowed_bss + 1,
                erase_size=erase_size,
            )

    def test_profile_deltas_measure_without_weakening_phase2_budget(self) -> None:
        document = CHECKER.load_baseline()
        profile = document["baseline"]["profiles"]["full"]
        acceptance = document["acceptance"]
        erase_size = 4096
        overage = acceptance["flash_delta_max_bytes"] + erase_size
        raw_bytes = (
            profile["image"]["flash_occupied_bytes"]
            + overage
            - CHECKER.build_firmware.K210_IMAGE_OVERHEAD
        )

        self.assertEqual(
            CHECKER.profile_deltas(
                document,
                "full",
                raw_bytes=raw_bytes,
                data_bytes=profile["elf"]["data_bytes"],
                bss_bytes=profile["elf"]["bss_bytes"],
                erase_size=erase_size,
            ),
            (overage, 0),
        )
        with self.assertRaisesRegex(RuntimeError, "flash delta"):
            CHECKER.verify_profile_budget(
                document,
                "full",
                raw_bytes=raw_bytes,
                data_bytes=profile["elf"]["data_bytes"],
                bss_bytes=profile["elf"]["bss_bytes"],
                erase_size=erase_size,
            )


if __name__ == "__main__":
    unittest.main()
