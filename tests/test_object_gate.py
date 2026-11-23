import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "firmware/src/apps/object_gate"
SPEC = importlib.util.spec_from_file_location("object_gate", ROOT / "examples/object_gate.py")
object_gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(object_gate)


def detection(box, score=600, label=14):
    return 1, 0, label, *box, score


class ObjectGateTests(unittest.TestCase):
    def test_python_and_c_match_on_the_same_detections(self):
        cc = os.environ.get("CC") or shutil.which("gcc")
        if not cc:
            self.skipTest("C compiler unavailable")
        person = detection((90, 95, 150, 225), score=500)
        better = detection((82, 92, 159, 227))
        wrong_class = detection((90, 80, 150, 230), score=900, label=4)
        outside = detection((180, 90, 200, 230), score=900)
        below_threshold = detection((90, 80, 150, 230), score=499)
        malformed = detection((100, 90, 242, 230), score=999)
        frames = [[], [wrong_class, outside, below_threshold, malformed],
                  [person], [person, better], [person],
                  [], [person], [person], [person], [], [], [],
                  [person], [person], [person]]
        gate = object_gate.Gate()
        digest = 0
        expected = []
        wire = []
        colors = []
        for items in frames:
            box, color = gate.step(240, 320, items)
            colors.append(color)
            digest = object_gate.snapshot_digest(digest, 240, 320, items)
            expected.append((*box, color, int(gate.occupied), gate.entries, digest))
            wire.append(f"240 320 {len(items)}")
            wire.extend(" ".join(map(str, item)) for item in items)
        self.assertEqual(colors, [object_gate.GUIDE, object_gate.GUIDE,
                                  object_gate.CANDIDATE, object_gate.CANDIDATE, object_gate.CONFIRMED,
                                  object_gate.GUIDE, object_gate.CANDIDATE, object_gate.CANDIDATE,
                                  object_gate.CONFIRMED, object_gate.GUIDE, object_gate.GUIDE,
                                  object_gate.GUIDE, object_gate.CANDIDATE, object_gate.CANDIDATE,
                                  object_gate.CONFIRMED])
        self.assertEqual(expected[3][:4], (82, 92, 77, 135))
        self.assertEqual([expected[i][5:7] for i in (4, 5, 8, 11, 14)],
                         [(1, 1), (1, 1), (1, 1), (0, 1), (1, 2)])

        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "object-gate.exe"
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "firmware/include"), "-I", str(CORE),
                            str(CORE / "object_gate_core.c"), str(ROOT / "tests/object_gate_harness.c"),
                            "-o", str(target)], check=True, cwd=ROOT)
            native = subprocess.run([str(target)], input="\n".join(wire) + "\n",
                                    text=True, check=True, capture_output=True, cwd=ROOT)
        self.assertEqual([tuple(map(int, line.split())) for line in native.stdout.splitlines()], expected)
