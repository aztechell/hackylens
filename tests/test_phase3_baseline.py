from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
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
    def test_current_source_obeys_every_zero_resource_rule(self) -> None:
        document = CHECKER.load_baseline()
        closure = document["baseline"]["closure"]["closure_commit"]
        historical = CHECKER.check_phase1_resources._baseline_source_snapshot(
            closure, root=ROOT
        )
        current = CHECKER.check_phase1_resources._current_source_snapshot(root=ROOT)
        observation = CHECKER.enforce_zero_resource_rules(
            document, historical, current
        )
        self.assertEqual(
            observation,
            {
                "new_heap_allocation_sites": [],
                "new_background_task_sites": [],
                "new_general_queue_sites": [],
                "new_runtime_core_starts": [],
                "additional_full_framebuffer_expressions": 0,
            },
        )

    def test_each_forbidden_resource_injection_fails_the_gate(self) -> None:
        document = CHECKER.load_baseline()
        historical = {"firmware/base.c": "void base(void) {}\n"}
        examples = {
            "new_heap_allocation_sites":
                "void phase3(void) { (void)malloc(64U); }\n",
            "new_background_task_sites":
                "void phase3(void) { xTaskCreate(worker, 0, 0, 0, 0, 0); }\n",
            "new_general_queue_sites":
                "void phase3(void) { (void)xQueueCreate(4U, 8U); }\n",
            "new_runtime_core_starts":
                "void phase3(void) { (void)hal_core1_start(worker, 0); }\n",
            "additional_full_framebuffer_expressions": (
                "static unsigned short extra[HK_DISPLAY_REQUIRED_WIDTH * "
                "HK_DISPLAY_REQUIRED_HEIGHT];\n"
            ),
        }
        for field, source in examples.items():
            with self.subTest(field=field):
                current = {**historical, "firmware/phase3.c": source}
                observation = CHECKER.zero_resource_observation(
                    historical, current
                )
                value = observation[field]
                self.assertGreater(len(value) if isinstance(value, list) else value, 0)
                with self.assertRaisesRegex(RuntimeError, field):
                    CHECKER.enforce_zero_resource_rules(
                        document, historical, current
                    )

    def test_const_descriptor_binding_is_not_a_new_resource_alias(self) -> None:
        historical = {
            "firmware/app.c": (
                "void *app_enter(void) { return malloc(64U); }\n"
            ),
        }
        current = {
            **historical,
            "firmware/registry.c": (
                "struct entry { void *(*enter)(void); };\n"
                "const struct entry app_entry = {.enter = app_enter};\n"
            ),
        }
        observation = CHECKER.zero_resource_observation(historical, current)
        self.assertEqual(observation["new_heap_allocation_sites"], [])

        current["firmware/registry.c"] += (
            "void wire(struct entry *entry) { entry->enter = app_enter; }\n"
        )
        self.assertTrue(
            CHECKER.zero_resource_observation(historical, current)[
                "new_heap_allocation_sites"
            ]
        )

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
