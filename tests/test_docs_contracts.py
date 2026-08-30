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


def adr(
    number: str,
    *,
    status: str = "accepted",
    supersedes: str = "",
    superseded_by: str = "",
) -> str:
    sections = "\n".join(
        f"## {section}\n\nEvidence for {section}.\n"
        for section in CHECK_DOCS.ADR_SECTIONS
    )
    return (
        "---\n"
        f"adr: {number}\n"
        f"title: Decision {number}\n"
        f"status: {status}\n"
        "date: 2026-08-11\n"
        "deciders: test-owner\n"
        f"supersedes: {supersedes}\n"
        f"superseded-by: {superseded_by}\n"
        "---\n\n"
        f"# ADR-{number}: Decision {number}\n\n"
        f"{sections}"
    )


class DocumentationContractsTest(unittest.TestCase):
    def test_repository_documentation_passes(self) -> None:
        self.assertEqual(CHECK_DOCS.check_repository(ROOT), [])

    def test_nested_spec_contracts_are_discovered(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-nested-") as temp:
            root = Path(temp)
            nested = write(
                root / "docs" / "spec" / "capabilities" / "EXAMPLE.md",
                contract("hackylens.capability.example"),
            )
            paths = CHECK_DOCS.contract_paths(root)
        self.assertIn(nested, paths)

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
                        "migration-guide: MIGRATION.md\n"
                    ),
                ),
            )
            write(root / "docs" / "MIGRATION.md", "# Move forward\n")
            issues, _ = CHECK_DOCS.validate_contract_documents(root, [source])
        self.assertTrue(any("at least 1.4.0" in found.message for found in issues))

    def test_deprecated_contract_requires_machine_readable_migration(self) -> None:
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
                        "removal-version: 1.4.0\n"
                    ),
                ),
            )
            issues, _ = CHECK_DOCS.validate_contract_documents(root, [source])
        self.assertTrue(
            any(
                "lacks migration-guide or replacement-contract" in found.message
                for found in issues
            )
        )

    def test_deprecated_contract_accepts_checked_migration_guide(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-deprecation-") as temp:
            root = Path(temp)
            write(root / "docs" / "MIGRATION.md", "# Old API\n")
            source = write(
                root / "docs" / "deprecated.md",
                contract(
                    "hackylens.deprecated",
                    version="1.3.2",
                    stability="deprecated",
                    extra=(
                        "deprecated-since: 1.3.2\n"
                        "removal-version: 1.4.0\n"
                        "migration-guide: MIGRATION.md#old-api\n"
                    ),
                ),
            )
            issues, _ = CHECK_DOCS.validate_contract_documents(root, [source])
        self.assertEqual(issues, [])

    def test_deprecated_contract_rejects_broken_migration_guide(self) -> None:
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
                        "removal-version: 1.4.0\n"
                        "migration-guide: missing.md#old-api\n"
                    ),
                ),
            )
            issues, _ = CHECK_DOCS.validate_contract_documents(root, [source])
        self.assertTrue(any("broken local link" in found.message for found in issues))

    def test_deprecated_contract_rejects_external_migration_guide(self) -> None:
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
                        "removal-version: 1.4.0\n"
                        "migration-guide: https://example.com/migrate\n"
                    ),
                ),
            )
            issues, _ = CHECK_DOCS.validate_contract_documents(root, [source])
        self.assertTrue(
            any("repository-local Markdown target" in found.message for found in issues)
        )

    def test_deprecated_contract_accepts_existing_replacement_contract(self) -> None:
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
                        "removal-version: 1.4.0\n"
                        "replacement-contract: hackylens.replacement\n"
                    ),
                ),
            )
            replacement = write(
                root / "docs" / "replacement.md",
                contract("hackylens.replacement"),
            )
            issues, _ = CHECK_DOCS.validate_contract_documents(
                root, [source, replacement]
            )
        self.assertEqual(issues, [])

    def test_deprecated_contract_rejects_unknown_replacement_contract(self) -> None:
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
                        "removal-version: 1.4.0\n"
                        "replacement-contract: hackylens.missing\n"
                    ),
                ),
            )
            issues, _ = CHECK_DOCS.validate_contract_documents(root, [source])
        self.assertTrue(any("unknown contract" in found.message for found in issues))

    def test_experimental_versioning_is_independent_of_major(self) -> None:
        policy = (ROOT / "docs" / "spec" / "VERSIONING.md").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "For an experimental contract at any major version, an intentional breaking\n"
            "  change increments MINOR",
            policy,
        )
        self.assertIn("For a stable contract, an incompatible change increments MAJOR", policy)

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
                "> HackyLens v0.4 is a layered K210 reference firmware and MicroPython\n"
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
                contract(
                    "hackylens.hmpy",
                    version="7.0.0",
                    extra="wire-major: 1\n",
                )
                + "# HackyLens MicroPython Protocol (HMPY) v1\n",
            )
            external = write(
                root / "docs" / "EXTERNAL_LINK_PROTOCOL.md",
                contract(
                    "hackylens.external-link",
                    version="1.0.0",
                    extra="wire-major: 1\n",
                )
                + "# HackyLens External Link Protocol v1\n",
            )
            ai = write(
                root / "docs" / "AI_MODELS.md",
                contract(
                    "hackylens.ai-model-package",
                    version="1.0.0",
                    extra="schema-major: 1\n",
                ),
            )
            micropython = write(
                root / "docs" / "MICROPYTHON_API.md",
                contract(
                    "hackylens.micropython-api",
                    version="1.0.0",
                    extra="api-major: 1\n",
                )
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
        self.assertFalse(any("7" in found.message and "HMPY" in found.message for found in issues))

    def test_phase3_incompatible_contract_version_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-phase3-version-") as temp:
            root = Path(temp)
            runtime = write(
                root / "docs" / "spec" / "APP_RUNTIME.md",
                contract(
                    "hackylens.app-runtime",
                    version="0.2.0",
                    extra=(
                        "phase: 3\n"
                        "compatibility-app-manifest: >=0.1.0,<0.2.0\n"
                        "compatibility-capability-api: >=0.1.0,<0.2.0\n"
                    ),
                ),
            )
            manifest = write(
                root / "docs" / "spec" / "APP_MANIFEST.md",
                contract(
                    "hackylens.native-app-manifest",
                    extra=(
                        "phase: 3\n"
                        "schema-major: 1\n"
                        "format-scope: native-app-build\n"
                        "runtime-parsed: false\n"
                        "compatibility-app-runtime: >=0.1.0,<0.2.0\n"
                        "compatibility-capability-api: >=0.1.0,<0.2.0\n"
                    ),
                ),
            )
            sdk = write(
                root / "docs" / "spec" / "APP_SDK.md",
                contract(
                    "hackylens.feature-app-sdk",
                    extra=(
                        "phase: 3\n"
                        "compatibility-app-runtime: >=0.1.0,<0.2.0\n"
                        "compatibility-app-manifest: >=0.1.0,<0.2.0\n"
                        "compatibility-capability-api: >=0.1.0,<0.2.0\n"
                    ),
                ),
            )
            capability = write(
                root / "docs" / "spec" / "CAPABILITY_API.md",
                contract("hackylens.capability-api"),
            )
            _, contracts = CHECK_DOCS.validate_contract_documents(
                root, [runtime, manifest, sdk, capability]
            )
            issues = CHECK_DOCS.check_phase3_contracts(root, contracts)
        messages = [found.message for found in issues]
        self.assertTrue(any("must remain on initial version 0.1.0" in item
                            for item in messages))
        self.assertTrue(any("does not accept hackylens.app-runtime version" in item
                            for item in messages))

    def test_phase3_forbidden_project_scope_is_rejected(self) -> None:
        manifest = ROOT / "docs" / "spec" / "APP_MANIFEST.md"
        text = manifest.read_text(encoding="utf-8")
        for forbidden in (
            '\n[runtime]\nruntime = "micropython"\nheap_bytes = 131072\n',
            "\nFirmware MUST parse app.toml at runtime.\n",
            "\ndynamic_loading = true\n",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertTrue(any(
                    pattern.search(text + forbidden)
                    for pattern in CHECK_DOCS.PHASE4_SCHEMA_PATTERNS
                ))

    def test_phase3_teardown_deadline_origin_and_no_refresh_are_required(self) -> None:
        originals = {
            relative: (ROOT / relative).read_text(encoding="utf-8")
            for relative in CHECK_DOCS.TEARDOWN_DEADLINE_REQUIREMENTS
        }
        mutations = (
            (
                Path("docs/spec/APP_RUNTIME.md"),
                "creates\nexactly one finite absolute monotonic teardown deadline",
                "creates\nan unspecified teardown limit",
                "single teardown-deadline origin",
            ),
            (
                Path("docs/spec/APP_RUNTIME.md"),
                "MUST NOT be refreshed between stages",
                "may be refreshed between stages",
                "prohibit stage/provider deadline refresh",
            ),
            (
                Path("docs/spec/APP_RUNTIME.md"),
                "hk_time_deadline_after_us",
                "an unspecified clock helper",
                "derive the deadline exactly once through Time API",
            ),
            (
                Path("docs/spec/APP_SDK.md"),
                "Repeated calls return the same",
                "Repeated calls may return a different",
                "preserve the no-refresh rule",
            ),
        )
        for relative, old, new, expected in mutations:
            with self.subTest(relative=relative, expected=expected):
                with tempfile.TemporaryDirectory(
                    prefix="hackylens-phase3-deadline-"
                ) as temp:
                    root = Path(temp)
                    for path, text in originals.items():
                        write(root / path, text)
                    changed = originals[relative].replace(old, new, 1)
                    self.assertNotEqual(changed, originals[relative])
                    write(root / relative, changed)
                    issues = CHECK_DOCS.check_phase3_teardown_deadline(root)
                self.assertTrue(any(
                    expected in found.message for found in issues
                ))

    def test_phase3_exact_context_grants_are_required(self) -> None:
        originals = {
            relative: (ROOT / relative).read_text(encoding="utf-8")
            for relative in CHECK_DOCS.APP_CONTEXT_GRANT_REQUIREMENTS
        }
        mutations = (
            (
                Path("docs/spec/APP_RUNTIME.md"),
                "Before `probe`, the runtime resolves",
                "After `probe`, the runtime resolves",
                "preflight grants without an owner before probe",
            ),
            (
                Path("docs/spec/APP_RUNTIME.md"),
                "`HK_ERR_NOT_DECLARED` is returned before provider access",
                "undeclared requests may reach provider access",
                "rejected before provider access",
            ),
            (
                Path("docs/spec/APP_SDK.md"),
                "Copying either the\ncontext or a handle does not extend",
                "Copying the context may extend",
                "reject copied stale authority",
            ),
            (
                Path("docs/adr/0007-adopt-generation-checked-app-lifecycle.md"),
                "After a successful\n`probe`, runtime opens one",
                "Before `probe`, runtime opens one",
                "post-probe exact grant injection",
            ),
            (
                Path("docs/spec/APP_RUNTIME.md"),
                "This availability is\nstable for the launch.",
                "This availability may change during the launch.",
                "optional availability must not become fallback",
            ),
            (
                Path("docs/spec/APP_RUNTIME.md"),
                "authoritative owner and\npreflight availability in private instance state",
                "owner and availability in the public context",
                "authority must remain private",
            ),
            (
                Path("docs/spec/APP_SDK.md"),
                "Lifecycle callbacks receive the context through a\n`const hk_app_context_t *`",
                "Lifecycle callbacks receive a mutable context",
                "callbacks must receive a const app context",
            ),
            (
                Path("docs/adr/0007-adopt-generation-checked-app-lifecycle.md"),
                "Preflight availability is stable",
                "Preflight availability may change",
                "keep probe availability stable",
            ),
            (
                Path("docs/adr/0007-adopt-generation-checked-app-lifecycle.md"),
                "authoritative owner and preflight availability in private instance state",
                "owner and availability in the public context",
                "grant authority private",
            ),
        )
        for relative, old, new, expected in mutations:
            with self.subTest(relative=relative, expected=expected):
                with tempfile.TemporaryDirectory(
                    prefix="hackylens-phase3-context-"
                ) as temp:
                    root = Path(temp)
                    for path, text in originals.items():
                        write(root / path, text)
                    changed = originals[relative].replace(old, new, 1)
                    self.assertNotEqual(changed, originals[relative])
                    write(root / relative, changed)
                    issues = CHECK_DOCS.check_phase3_app_context_grants(root)
                self.assertTrue(any(
                    expected in found.message for found in issues
                ))

    def test_native_app_manifest_schema_safety_rules_are_normative(self) -> None:
        relative = Path("docs/spec/APP_MANIFEST.md")
        original = (ROOT / relative).read_text(encoding="utf-8")
        mutations = (
            (
                "every field is required",
                "fields may be omitted",
                "exact fields without defaults",
            ),
            (
                "symlink/junction escape",
                "ordinary path",
                "real-directory path confinement",
            ),
            (
                "Firmware does not read its TOML input",
                "Firmware may read its TOML input",
                "remain build-time-only",
            ),
            (
                "1024 UTF-8 bytes",
                "an implementation-defined number of bytes",
                "metadata string bounds",
            ),
            (
                "MUST NOT generate an SDK/runtime handle",
                "may generate a runtime handle",
                "confine transitional legacy services",
            ),
            (
                "python tools/gen_app_composition.py --check",
                "an unspecified generator command",
                "composition freshness from one canonical model",
            ),
            (
                "an undeclared header\ndirectory MUST NOT enter",
                "an undeclared header\ndirectory MAY enter",
                "govern C/C++ ownership and private include roots",
            ),
            (
                "never guesses callback\nsymbol names",
                "guesses callback\nsymbol names",
                "define typed legacy/v2 entry objects",
            ),
            (
                "no manual central app descriptor table",
                "a manual central app descriptor table is also maintained",
                "generated descriptors as the sole registry",
            ),
        )
        for old, new, expected in mutations:
            with self.subTest(expected=expected), tempfile.TemporaryDirectory(
                prefix="hackylens-app-manifest-doc-"
            ) as temp:
                root = Path(temp)
                changed = original.replace(old, new, 1)
                self.assertNotEqual(original, changed)
                write(root / relative, changed)
                issues = CHECK_DOCS.check_app_manifest_schema(root)
                self.assertTrue(any(
                    expected in found.message for found in issues
                ))

    def test_completed_phase2_rejects_stale_firmware_status(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-phase2-status-") as temp:
            root = Path(temp)
            write(
                root / "docs" / "CURRENT_STATE.md",
                "Phase 2 is complete.\n",
            )
            write(
                root / "docs" / "ROADMAP.md",
                "Статус: **DONE**.\n",
            )
            versioning = root / "docs" / "spec" / "VERSIONING.md"
            write(
                versioning,
                "Firmware 0.4.0: physical qualification in progress.\n",
            )
            stale = CHECK_DOCS.check_phase2_status_consistency(root)
            write(
                versioning,
                "Firmware 0.4.0: Phase 2 physically accepted on SEN0305; "
                "Maix Cube compile-conformance-only; general hardware "
                "portability not claimed.\n",
            )
            current = CHECK_DOCS.check_phase2_status_consistency(root)
        self.assertTrue(any(
            "contradicts completed Phase 2" in found.message for found in stale
        ))
        self.assertEqual(current, [])

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

    def test_malformed_adr_filename_and_duplicate_number_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-adr-number-") as temp:
            root = Path(temp)
            write(root / "docs" / "adr" / "02-short.md", adr("0002"))
            write(root / "docs" / "adr" / "0001-first.md", adr("0001"))
            write(root / "docs" / "adr" / "0001-second.md", adr("0001"))
            issues = CHECK_DOCS.check_adrs(root)
        messages = [found.message for found in issues]
        self.assertTrue(any("invalid ADR filename" in item for item in messages))
        self.assertTrue(any("duplicate ADR number 0001" in item for item in messages))

    def test_adr_superseding_references_must_exist_and_be_reciprocal(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-adr-links-") as temp:
            root = Path(temp)
            write(
                root / "docs" / "adr" / "0001-original.md",
                adr("0001", status="accepted"),
            )
            write(
                root / "docs" / "adr" / "0002-replacement.md",
                adr("0002", supersedes="0001"),
            )
            write(
                root / "docs" / "adr" / "0003-missing.md",
                adr("0003", supersedes="9999"),
            )
            issues = CHECK_DOCS.check_adrs(root)
        messages = [found.message for found in issues]
        self.assertTrue(any("must declare superseded-by: 0002" in item for item in messages))
        self.assertTrue(any("must have status superseded" in item for item in messages))
        self.assertTrue(any("references missing ADR 9999" in item for item in messages))

    def test_reciprocal_adr_superseding_pair_passes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-doc-adr-pair-") as temp:
            root = Path(temp)
            write(
                root / "docs" / "adr" / "0001-original.md",
                adr("0001", status="superseded", superseded_by="0002"),
            )
            write(
                root / "docs" / "adr" / "0002-replacement.md",
                adr("0002", supersedes="0001"),
            )
            issues = CHECK_DOCS.check_adrs(root)
        self.assertEqual(issues, [])

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
