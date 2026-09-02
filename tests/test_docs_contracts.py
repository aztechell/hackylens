from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load_checker():
    path = ROOT / "tools" / "check_docs.py"
    spec = importlib.util.spec_from_file_location("hackylens_check_docs", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


CHECK_DOCS = load_checker()


def write(path: Path, content: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path


class DocumentationContractsTest(unittest.TestCase):
    def test_repository_documentation_passes(self) -> None:
        self.assertEqual(CHECK_DOCS.check_repository(ROOT), [])

    def test_broken_local_link_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-link-") as temp:
            root = Path(temp)
            source = write(root / "docs" / "source.md", "[missing](target.md)\n")
            issues = CHECK_DOCS.check_links(root, [source])
        self.assertEqual(len(issues), 1)
        self.assertIn("broken local link", issues[0].message)

    def test_markdown_fragment_is_checked(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-anchor-") as temp:
            root = Path(temp)
            write(root / "docs" / "target.md", "# Existing Heading\n")
            source = write(
                root / "docs" / "source.md",
                "[valid](target.md#existing-heading)\n\n"
                "[invalid](target.md#missing-heading)\n",
            )
            issues = CHECK_DOCS.check_links(root, [source])
        self.assertEqual(len(issues), 1)
        self.assertIn("missing Markdown anchor", issues[0].message)

    def test_reference_style_markdown_links_are_checked(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-reference-") as temp:
            root = Path(temp)
            write(root / "docs" / "target.md", "# Existing Heading\n")
            source = write(
                root / "docs" / "source.md",
                "[valid][architecture]\n\n"
                "[collapsed][]\n\n"
                "[missing][unknown]\n\n"
                "[architecture]: target.md#existing-heading\n"
                "[collapsed]: missing.md\n",
            )
            issues = CHECK_DOCS.check_links(root, [source])
        messages = [found.message for found in issues]
        self.assertTrue(any("missing Markdown reference definition" in item for item in messages))
        self.assertTrue(any("broken local link" in item for item in messages))
        self.assertEqual(len(issues), 2)

    def test_forbidden_claim_requires_same_paragraph_qualifier(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-claim-") as temp:
            root = Path(temp)
            bad = write(
                root / "bad.md",
                "HackyLens is an open application standard.\n",
            )
            good = write(
                root / "good.md",
                "HackyLens is a candidate open application standard.\n",
            )
            bad_issues = CHECK_DOCS.check_forbidden_claims(root, [bad])
            good_issues = CHECK_DOCS.check_forbidden_claims(root, [good])
        self.assertEqual(len(bad_issues), 1)
        self.assertEqual(good_issues, [])

    def test_fixed_canonical_version_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-version-") as temp:
            root = Path(temp)
            write(root / "VERSION", "0.2.0\n")
            write(
                root / "README.md",
                "Firmware version 0.2.0\nfirmware-v0.2.0\n",
            )
            write(
                root / "docs" / "HMPY_PROTOCOL.md",
                "# HackyLens MicroPython Protocol (HMPY) v1\n",
            )
            write(
                root / "docs" / "EXTERNAL_LINK_PROTOCOL.md",
                "# HackyLens External Link Protocol v1\n",
            )
            write(
                root / "docs" / "MICROPYTHON_API.md",
                "# HackyLens MicroPython API v1\n",
            )
            write(root / "tools" / "hmpy_protocol.py", "PROTOCOL_VERSION = 2\n")
            write(
                root / "firmware" / "src" / "services" / "hmpy_codec.h",
                "#define HMPY_PROTOCOL_VERSION 1U\n",
            )
            write(
                root / "firmware" / "src" / "services" / "external_link_protocol.h",
                "#define HK_LINK_PROTOCOL_VERSION 1U\n",
            )
            write(root / "tools" / "ai_model.py", "MANIFEST_VERSION = 1\n")
            write(
                root / "firmware" / "src" / "storage" / "ai_model_storage.c",
                "#define AI_MANIFEST_VERSION 1U\n",
            )
            issues = CHECK_DOCS.check_canonical_versions(root)
        self.assertTrue(any("HMPY host version 2" in found.message for found in issues))


if __name__ == "__main__":
    unittest.main()
