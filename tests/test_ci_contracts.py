from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import re
import subprocess
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
    def test_host_gcc_dependency_is_version_and_content_pinned(self) -> None:
        bootstrap = load_tool("bootstrap_deps")
        self.assertEqual(bootstrap.HOST_GCC_VERSION, "13.2.0")
        self.assertEqual(bootstrap.HOST_GCC_MACHINE, "x86_64-w64-mingw32")
        self.assertIn("/13.2.0-rt_v11-rev0/", bootstrap.HOST_GCC_URL)
        self.assertRegex(bootstrap.HOST_GCC_SHA256, re.compile(r"^[0-9a-f]{64}$"))

    def test_zero_before_and_dispatch_ranges_cover_multi_commit_new_branch(self) -> None:
        selector = load_tool("ci_diff_range")
        with tempfile.TemporaryDirectory(prefix="hackylens-diff-range-") as directory:
            repository = Path(directory)
            subprocess.run(
                ["git", "init", "--quiet", "--initial-branch=main"],
                cwd=repository,
                check=True,
            )
            environment = {
                **os.environ,
                "GIT_AUTHOR_NAME": "CI Range Test",
                "GIT_AUTHOR_EMAIL": "range@example.invalid",
                "GIT_COMMITTER_NAME": "CI Range Test",
                "GIT_COMMITTER_EMAIL": "range@example.invalid",
            }

            def commit(name: str, content: str) -> str:
                (repository / name).write_text(content, encoding="utf-8")
                subprocess.run(["git", "add", name], cwd=repository, check=True)
                subprocess.run(
                    ["git", "commit", "--quiet", "-m", name],
                    cwd=repository, env=environment, check=True,
                )
                return subprocess.run(
                    ["git", "rev-parse", "HEAD"], cwd=repository,
                    text=True, capture_output=True, check=True,
                ).stdout.strip()

            commit("root.txt", "root\n")
            default_tip = commit("main.txt", "main\n")
            subprocess.run(
                ["git", "update-ref", "refs/remotes/origin/main", default_tip],
                cwd=repository, check=True,
            )
            subprocess.run(
                ["git", "switch", "--quiet", "-c", "feature"],
                cwd=repository, check=True,
            )
            commit("first.txt", "first\n")
            commit("second.txt", "second\n")
            expected = f"{default_tip}..HEAD"
            for event in ("push", "workflow_dispatch"):
                with self.subTest(event=event):
                    selected = selector.select_range(
                        event,
                        push_before_sha=selector.ZERO_SHA,
                        default_branch="main",
                        root=repository,
                    )
                    self.assertEqual(selected, expected)
                    changed = subprocess.run(
                        ["git", "diff", "--name-only", selected],
                        cwd=repository, text=True, capture_output=True, check=True,
                    ).stdout.splitlines()
                    self.assertEqual(changed, ["first.txt", "second.txt"])


if __name__ == "__main__":
    unittest.main()
