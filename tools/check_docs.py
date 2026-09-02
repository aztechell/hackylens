#!/usr/bin/env python3
"""Validate local documentation links, public versions, and a few product claims."""

from __future__ import annotations

from pathlib import Path
import re
import sys
from typing import Iterable, NamedTuple
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]

RELEASE_SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
MARKDOWN_LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
REFERENCE_LINK_RE = re.compile(r"!?\[([^\]]*)\]\[([^\]]*)\]")
REFERENCE_DEFINITION_RE = re.compile(r"^\s{0,3}\[([^\]]+)\]:\s*(.+?)\s*$")
HEADING_RE = re.compile(r"^\s{0,3}(#{1,6})\s+(.+?)\s*#*\s*$")

CLAIM_SURFACES = (
    Path("README.md"),
    Path("docs/ARCHITECTURE.md"),
    Path("docs/MODULES.md"),
)

FORBIDDEN_CLAIMS = (
    "openmv-class",
    "hardware-independent platform",
    "hardware-independent robotics platform",
    "open application standard",
)

CLAIM_QUALIFIERS = (
    "candidate",
    "target",
    "roadmap goal",
    "not yet",
    "does not yet",
    "not demonstrated",
)


class Issue(NamedTuple):
    path: Path
    line: int
    message: str


def relative_path(root: Path, path: Path) -> Path:
    try:
        return path.resolve().relative_to(root.resolve())
    except ValueError:
        return path


def issue(root: Path, path: Path, line: int, message: str) -> Issue:
    return Issue(relative_path(root, path), line, message)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def markdown_files(root: Path) -> list[Path]:
    files: set[Path] = set()
    for direct in (root / "README.md", root / "isp_stub" / "NOTICE.md"):
        if direct.is_file():
            files.add(direct)
    for directory in ("docs", "models", "sdcard", ".github"):
        base = root / directory
        if base.is_dir():
            files.update(base.rglob("*.md"))
    return sorted(files)


def lines_outside_fences(text: str) -> list[tuple[int, str]]:
    result: list[tuple[int, str]] = []
    fence: str | None = None
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.lstrip()
        marker = stripped[:3]
        if marker in ("```", "~~~"):
            if fence is None:
                fence = marker
            elif fence == marker:
                fence = None
            continue
        if fence is None:
            result.append((number, line))
    return result


def heading_anchors(text: str) -> set[str]:
    anchors: set[str] = set()
    counts: dict[str, int] = {}
    for _, line in lines_outside_fences(text):
        match = HEADING_RE.match(line)
        if not match:
            continue
        heading = re.sub(r"[`*_~]", "", match.group(2)).lower()
        heading = re.sub(r"[^\w\- ]", "", heading, flags=re.UNICODE)
        base = re.sub(r"\s+", "-", heading.strip())
        count = counts.get(base, 0)
        anchor = base if count == 0 else f"{base}-{count}"
        counts[base] = count + 1
        anchors.add(anchor)
    return anchors


def link_target(raw: str) -> str:
    value = raw.strip()
    if value.startswith("<") and ">" in value:
        return value[1 : value.index(">")]
    match = re.match(r"([^\s]+)", value)
    return match.group(1) if match else value


def reference_label(raw: str) -> str:
    return re.sub(r"\s+", " ", raw.strip()).casefold()


def check_link_target(
    root: Path,
    path: Path,
    number: int,
    target: str,
    anchor_cache: dict[Path, set[str]],
) -> list[Issue]:
    lower = target.lower()
    if lower.startswith(("http://", "https://", "mailto:", "data:")):
        return []

    decoded = unquote(target)
    location, separator, fragment = decoded.partition("#")
    location = location.split("?", 1)[0]
    destination = path if not location else (path.parent / location).resolve()

    try:
        destination.relative_to(root.resolve())
    except ValueError:
        return [issue(root, path, number, f"local link escapes repository: {target!r}")]

    if not destination.exists():
        return [issue(root, path, number, f"broken local link: {target!r}")]

    if separator and fragment and destination.is_file():
        anchor = fragment.lower()
        if destination not in anchor_cache:
            anchor_cache[destination] = heading_anchors(read_text(destination))
        if anchor not in anchor_cache[destination]:
            return [issue(root, path, number, f"missing Markdown anchor: {target!r}")]
    return []


def check_links(root: Path, paths: Iterable[Path]) -> list[Issue]:
    issues: list[Issue] = []
    anchor_cache: dict[Path, set[str]] = {}

    for path in paths:
        text = read_text(path)
        source_lines = lines_outside_fences(text)
        definitions: dict[str, tuple[int, str]] = {}

        for number, line in source_lines:
            match = REFERENCE_DEFINITION_RE.match(line)
            if not match or match.group(1).startswith("^"):
                continue
            label = reference_label(match.group(1))
            target = link_target(match.group(2))
            if label in definitions:
                issues.append(
                    issue(root, path, number, f"duplicate Markdown reference definition: {label!r}")
                )
            else:
                definitions[label] = (number, target)

        for number, line in source_lines:
            for match in MARKDOWN_LINK_RE.finditer(line):
                target = link_target(match.group(1))
                issues.extend(check_link_target(root, path, number, target, anchor_cache))

            for match in REFERENCE_LINK_RE.finditer(line):
                label = reference_label(match.group(2) or match.group(1))
                if label not in definitions:
                    issues.append(
                        issue(root, path, number, f"missing Markdown reference definition: {label!r}")
                    )

        for number, target in definitions.values():
            issues.extend(check_link_target(root, path, number, target, anchor_cache))
    return issues


def markdown_paragraphs(text: str) -> list[tuple[int, str]]:
    paragraphs: list[tuple[int, str]] = []
    current: list[str] = []
    start = 1
    for number, line in lines_outside_fences(text):
        if line.strip():
            if not current:
                start = number
            current.append(line.strip())
        elif current:
            paragraphs.append((start, " ".join(current)))
            current = []
    if current:
        paragraphs.append((start, " ".join(current)))
    return paragraphs


def check_forbidden_claims(root: Path, paths: Iterable[Path]) -> list[Issue]:
    issues: list[Issue] = []
    for path in paths:
        if not path.is_file():
            issues.append(issue(root, path, 1, "required claim surface is missing"))
            continue
        for number, paragraph in markdown_paragraphs(read_text(path)):
            lowered = paragraph.lower()
            claim = next((item for item in FORBIDDEN_CLAIMS if item in lowered), None)
            if claim and not any(item in lowered for item in CLAIM_QUALIFIERS):
                issues.append(
                    issue(root, path, number, f"unqualified forbidden claim {claim!r}")
                )
    return issues


def fixed_constant(
    root: Path, relative: str, pattern: str, label: str
) -> tuple[int | None, list[Issue]]:
    path = root / relative
    if not path.is_file():
        return None, [issue(root, path, 1, f"canonical {label} source is missing")]
    match = re.search(pattern, read_text(path), flags=re.MULTILINE)
    if not match:
        return None, [issue(root, path, 1, f"canonical {label} constant is missing")]
    return int(match.group(1)), []


def check_canonical_versions(root: Path) -> list[Issue]:
    issues: list[Issue] = []
    version_path = root / "VERSION"
    if not version_path.is_file():
        issues.append(issue(root, version_path, 1, "canonical firmware VERSION is missing"))
    else:
        firmware_version = read_text(version_path).strip()
        if not RELEASE_SEMVER_RE.fullmatch(firmware_version):
            issues.append(issue(root, version_path, 1, "firmware VERSION is not release SemVer"))
        readme = root / "README.md"
        if readme.is_file():
            readme_text = read_text(readme)
            for expected in (
                f"Firmware version {firmware_version}",
                f"firmware-v{firmware_version}",
            ):
                if expected not in readme_text:
                    issues.append(issue(root, readme, 1, f"README badge does not contain {expected!r}"))
        else:
            issues.append(issue(root, readme, 1, "README is missing"))

    families = (
        (
            (
                ("tools/hmpy_protocol.py", r"^PROTOCOL_VERSION\s*=\s*(\d+)\s*$", "HMPY host version"),
                ("firmware/src/services/hmpy_codec.h", r"^#define\s+HMPY_PROTOCOL_VERSION\s+(\d+)U?\s*$", "HMPY firmware version"),
            ),
            root / "docs" / "HMPY_PROTOCOL.md",
            r"^# HackyLens MicroPython Protocol \(HMPY\) v(\d+)\s*$",
        ),
        (
            (
                ("firmware/src/services/external_link_protocol.h", r"^#define\s+HK_LINK_PROTOCOL_VERSION\s+(\d+)U?\s*$", "External Link version"),
            ),
            root / "docs" / "EXTERNAL_LINK_PROTOCOL.md",
            r"^# HackyLens External Link Protocol v(\d+)\s*$",
        ),
        (
            (
                ("tools/ai_model.py", r"^MANIFEST_VERSION\s*=\s*(\d+)\s*$", "AI host manifest version"),
                ("firmware/src/storage/ai_model_storage.c", r"^#define\s+AI_MANIFEST_VERSION\s+(\d+)U?\s*$", "AI firmware manifest version"),
            ),
            None,
            None,
        ),
        (
            (),
            root / "docs" / "MICROPYTHON_API.md",
            r"^# HackyLens MicroPython API v(\d+)\s*$",
        ),
    )

    for constants, heading_path, heading_pattern in families:
        observed: list[tuple[Path, str, int]] = []
        for relative, pattern, label in constants:
            value, constant_issues = fixed_constant(root, relative, pattern, label)
            issues.extend(constant_issues)
            if value is not None:
                observed.append((root / relative, label, value))
        if heading_path is not None and heading_pattern is not None:
            if not heading_path.is_file():
                issues.append(issue(root, heading_path, 1, "versioned contract heading is missing"))
            else:
                match = re.search(heading_pattern, read_text(heading_path), flags=re.MULTILINE)
                if not match:
                    issues.append(issue(root, heading_path, 1, "versioned contract heading is missing"))
                else:
                    observed.append((heading_path, "document heading", int(match.group(1))))
        majors = {value for _, _, value in observed}
        if len(majors) > 1:
            expected = next(iter(sorted(majors)))
            for path, label, value in observed:
                if value != expected:
                    issues.append(
                        issue(
                            root,
                            path,
                            1,
                            f"{label} {value} does not match public major {expected}",
                        )
                    )
    return issues


def check_repository(root: Path = ROOT) -> list[Issue]:
    root = root.resolve()
    issues = check_links(root, markdown_files(root))
    issues.extend(check_forbidden_claims(root, (root / path for path in CLAIM_SURFACES)))
    issues.extend(check_canonical_versions(root))
    return sorted(issues, key=lambda item: (str(item.path), item.line, item.message))


def main() -> int:
    issues = check_repository()
    if issues:
        for found in issues:
            print(f"{found.path.as_posix()}:{found.line}: {found.message}", file=sys.stderr)
        print(f"[FAIL] documentation guard found {len(issues)} issue(s)", file=sys.stderr)
        return 1
    print("[OK] documentation guard passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
