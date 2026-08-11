from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load_tool(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CiContractsTest(unittest.TestCase):
    def test_strict_runner_rejects_skips(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-strict-tests-") as temp:
            tests = Path(temp)
            source = tests / "test_sample.py"
            source.write_text(
                "import unittest\n"
                "class Sample(unittest.TestCase):\n"
                "    def test_skip(self):\n"
                "        self.skipTest('coverage unavailable')\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "run_tests.py"),
                    "--start-directory",
                    str(tests),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
        self.assertEqual(result.returncode, 1)
        self.assertIn("strict test run skipped 1 test(s)", result.stderr)

    def test_strict_runner_accepts_complete_suite(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-strict-tests-") as temp:
            tests = Path(temp)
            (tests / "test_sample.py").write_text(
                "import unittest\n"
                "class Sample(unittest.TestCase):\n"
                "    def test_pass(self):\n"
                "        self.assertTrue(True)\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "run_tests.py"),
                    "--start-directory",
                    str(tests),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_firmware_repo_does_not_build_or_package_the_standalone_ide(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )
        packager = (ROOT / "tools" / "package_release.py").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("bootstrap_ide", workflow)
        self.assertNotIn("hackylens-code", workflow)
        self.assertNotIn("actions/setup-node", workflow)
        self.assertNotIn("--ide", packager)
        self.assertNotIn("code.zip", packager)

    def test_release_workflow_validates_every_change_and_publishes_tags_only(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("  pull_request:\n", workflow)
        self.assertIn('      - "**"\n', workflow)
        self.assertIn("python tools/check_docs.py", workflow)
        self.assertIn("python tools/run_tests.py", workflow)
        self.assertIn("full --disable-app micropython", workflow)
        self.assertIn("--expect absent", workflow)
        self.assertIn("--expect present", workflow)
        self.assertGreaterEqual(
            workflow.count("github.event_name == 'push' && github.ref_type == 'tag'"),
            3,
        )
        self.assertIn("path: dist/release/", workflow)
        self.assertIn("dist/release/*", workflow)
        self.assertIn("hashFiles('tools/bootstrap_deps.py')", workflow)


if __name__ == "__main__":
    unittest.main()
