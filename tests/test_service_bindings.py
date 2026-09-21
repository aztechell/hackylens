from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from board_contract import load_board
import app_composition
import service_bindings as bindings

class ServiceBindingTests(unittest.TestCase):
    def test_board_selection_and_required_app_failure(self):
        runtime = bindings.select(load_board("huskylens-sen0305"), set(), set(), set())
        self.assertEqual({item.name for item in runtime.bindings},
                         {"time", "input", "lights", "display", "external-link"})
        self.assertFalse(runtime.disabled_apps)
        cube = bindings.select(load_board("sipeed-maix-cube"), set(), set(), set())
        self.assertEqual([item.name for item in cube.bindings], ["time"])
        self.assertEqual(cube.disabled_apps,
                         frozenset(app_composition.app_map(app_composition.load_model())))
        with self.assertRaisesRegex(ValueError, "required app"):
            bindings.select(load_board("sipeed-maix-cube"), set(), {"camera"}, set())

    def test_excluded_binding_cannot_be_satisfied_by_present_resource(self):
        selection = bindings.select(load_board("huskylens-sen0305"), set(), set(), {"display"})
        self.assertEqual(len(selection.disabled_apps), 12)
        self.assertIn("hk_display_binding = {0}", bindings.generated_c(selection))
        with self.assertRaisesRegex(ValueError, "Time"):
            bindings.select(load_board("huskylens-sen0305"), set(), set(), {"time"})

    def test_missing_route_excludes_provider_and_its_required_consumer(self):
        board = load_board("huskylens-sen0305")
        routes = [r for r in board.selected_routes() if r["id"] != "external-uart-rx"]
        with patch.object(type(board), "selected_routes", return_value=routes):
            selection = bindings.select(board, set(), set(), set())
        self.assertNotIn("external-link", {item.name for item in selection.bindings})
        self.assertIn("micropython", selection.disabled_apps)
        self.assertEqual(selection.absences[0]["code"], "route-unavailable")

    def test_evidence_and_absent_bindings_are_deterministic(self):
        selection = bindings.select(load_board("sipeed-maix-cube"), set(), set(), set())
        with tempfile.TemporaryDirectory() as directory:
            paths = bindings.write_artifacts(selection, Path(directory))
            first = [p.read_bytes() for p in paths]
            bindings.write_artifacts(selection, Path(directory))
            self.assertEqual(first, [p.read_bytes() for p in paths])
        source = bindings.generated_c(selection)
        self.assertNotIn("inventory", source)
        self.assertNotIn("grant", source)
        self.assertEqual(source.count(" = {0};"), 4)

if __name__ == "__main__":
    unittest.main()
