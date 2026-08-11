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


def contract(
    contract_id: str,
    *,
    version: str = "0.1.0",
    stability: str = "experimental",
    extra: str = "",
) -> str:
    return (
        "---\n"
        f"contract-id: {contract_id}\n"
        "owner: test-owner\n"
        f"version: {version}\n"
        f"stability: {stability}\n"
        f"{extra}"
        "---\n\n"
        "# Contract\n"
    )


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

    def test_invalid_metadata_and_duplicate_contract_id_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-meta-") as temp:
            root = Path(temp)
            first = write(root / "docs" / "first.md", contract("hackylens.same"))
            second = write(
                root / "docs" / "second.md",
                contract("hackylens.same", version="one"),
            )
            issues, _ = CHECK_DOCS.validate_contract_documents(root, [first, second])
        messages = [found.message for found in issues]
        self.assertTrue(any("invalid semantic version" in item for item in messages))
        self.assertTrue(any("duplicate contract-id" in item for item in messages))

    def test_deprecation_window_is_enforced(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-deprecation-") as temp:
            root = Path(temp)
            source = write(
                root / "docs" / "deprecated.md",
                contract(
                    "hackylens.deprecated",
                    version="1.3.2",
                    stability="deprecated",
                    extra=(
                        "deprecated-since: 1.3.2\n"
                        "removal-version: 1.3.9\n"
                    ),
                ),
            )
            issues, _ = CHECK_DOCS.validate_contract_documents(root, [source])
        self.assertTrue(any("at least 1.4.0" in found.message for found in issues))

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

    def test_preview_marker_is_required(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-preview-") as temp:
            root = Path(temp)
            bad = write(root / "bad.md", "HackyLens documentation.\n")
            good = write(
                root / "good.md",
                "> HackyLens v0.2 is a layered K210 reference firmware and MicroPython\n"
                "> technology preview.\n",
            )
            bad_issues = CHECK_DOCS.check_preview_markers(root, [bad])
            good_issues = CHECK_DOCS.check_preview_markers(root, [good])
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
            hmpy = write(
                root / "docs" / "HMPY_PROTOCOL.md",
                contract("hackylens.hmpy", version="1.0.0")
                + "# HackyLens MicroPython Protocol (HMPY) v1\n",
            )
            external = write(
                root / "docs" / "EXTERNAL_LINK_PROTOCOL.md",
                contract("hackylens.external-link", version="1.0.0")
                + "# HackyLens External Link Protocol v1\n",
            )
            ai = write(
                root / "docs" / "AI_MODELS.md",
                contract("hackylens.ai-model-package", version="1.0.0"),
            )
            micropython = write(
                root / "docs" / "MICROPYTHON_API.md",
                contract("hackylens.micropython-api", version="1.0.0")
                + "# HackyLens MicroPython API v1\n",
            )
            write(root / "tools" / "hmpy_protocol.py", "PROTOCOL_VERSION = 2\n")
            write(root / "firmware" / "src" / "services" / "hmpy_codec.h", "#define HMPY_PROTOCOL_VERSION 1U\n")
            write(root / "firmware" / "src" / "services" / "external_link_protocol.h", "#define HK_LINK_PROTOCOL_VERSION 1U\n")
            write(root / "tools" / "ai_model.py", "MANIFEST_VERSION = 1\n")
            write(root / "firmware" / "src" / "storage" / "ai_model_storage.c", "#define AI_MANIFEST_VERSION 1U\n")
            paths = [hmpy, external, ai, micropython]
            _, contracts = CHECK_DOCS.validate_contract_documents(root, paths)
            issues = CHECK_DOCS.check_canonical_versions(root, contracts)
        self.assertTrue(any("HMPY host version 2" in found.message for found in issues))

    def test_malformed_adr_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-adr-") as temp:
            root = Path(temp)
            write(
                root / "docs" / "adr" / "0001-test.md",
                "---\n"
                "adr: 0001\n"
                "title: Test\n"
                "status: done\n"
                "date: tomorrow\n"
                "deciders: test-owner\n"
                "---\n\n"
                "# ADR-0001: Test\n\n"
                "## Context\n",
            )
            issues = CHECK_DOCS.check_adrs(root)
        messages = [found.message for found in issues]
        self.assertTrue(any("invalid ADR status" in item for item in messages))
        self.assertTrue(any("ADR date" in item for item in messages))
        self.assertTrue(any("missing ADR section" in item for item in messages))

    def test_pull_request_template_captures_governance_evidence(self) -> None:
        template = (ROOT / ".github" / "pull_request_template.md").read_text(
            encoding="utf-8"
        )
        for token in (
            "Affected layers",
            "Affected capabilities",
            "Contract ID",
            "Compatibility/migration impact",
            "ADR:",
            "Flash/static RAM/stack/latency impact",
            "Hardware acceptance",
        ):
            self.assertIn(token, template)


if __name__ == "__main__":
    unittest.main()
