"""Firmware artifact measurements and source allocation regression checks."""
from __future__ import annotations
from collections import Counter
import hashlib
import io
import math
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
from typing import Any
import build_firmware
from board_contract import load_board

ROOT = Path(__file__).resolve().parents[1]
BOARD_ID = "huskylens-sen0305"
SOURCE_ROOTS = ("firmware", "boards", "platforms")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
DIRECT_RUNTIME_CALLS = {
    "malloc", "calloc", "realloc", "aligned_alloc", "pvportmalloc",
    "strdup", "_strdup",
    "xtaskcreate", "xtaskcreatestatic", "xqueuecreate", "xqueuecreatestatic",
    "pthread_create", "thrd_create", "std::thread",
    "make_unique", "make_shared",
}
WRAPPER_RUNTIME_MARKERS = ("alloc", "task", "queue", "spawn", "thread", "worker")
CALL_RE = re.compile(
    r"(?<![A-Za-z0-9_])(?P<name>(?:::)?[A-Za-z_][A-Za-z0-9_]*"
    r"(?:::[A-Za-z_][A-Za-z0-9_]*)*)\s*"
    r"(?:<[^;{}()]*>)?\s*\("
)
CXX_NEW_RE = re.compile(
    r"(?<![A-Za-z0-9_])(?:new(?:\s*\([^()]*\))?\s+"
    r"(?:const\s+|volatile\s+)*[A-Za-z_:]|::\s*operator\s+new\s*\()",
    re.MULTILINE,
)
STD_THREAD_OBJECT_RE = re.compile(
    r"(?<![A-Za-z0-9_])std\s*::\s*thread\s+"
    r"[A-Za-z_][A-Za-z0-9_]*\s*(?:\(|\{)",
    re.MULTILINE,
)
ALIAS_RE = re.compile(
    r"(?<![A-Za-z0-9_])(?P<alias>[A-Za-z_][A-Za-z0-9_]*)\s*"
    r"(?:\)\s*\([^;=]*\))?\s*=\s*&?\s*"
    r"(?P<target>(?:::)?[A-Za-z_][A-Za-z0-9_]*"
    r"(?:::[A-Za-z_][A-Za-z0-9_]*)*)\b(?!\s*(?:<[^;{}()]*>)?\s*\()"
)
MACRO_RE = re.compile(
    r"(?m)^\s*#\s*define\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"(?:\s*\([^\r\n]*?\))?\s+(?P<body>[^\r\n]+)$"
)
FUNCTION_RE = re.compile(
    r"(?P<name>(?:::)?[A-Za-z_][A-Za-z0-9_]*"
    r"(?:::[A-Za-z_~][A-Za-z0-9_]*)*)\s*"
    r"\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{",
    re.MULTILINE,
)
CONTROL_NAMES = {
    "if", "for", "while", "switch", "catch", "sizeof", "alignof",
    "decltype", "static_assert", "return", "defined",
}
TRIGRAPHS = {
    "??=": "#", "??/": "\\", "??'": "^", "??(": "[", "??)": "]",
    "??!": "|", "??<": "{", "??>": "}", "??-": "~",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_size() -> Path:
    toolchain = build_firmware.find_toolchain_bin()
    if toolchain:
        for name in ("riscv64-unknown-elf-size.exe", "riscv64-unknown-elf-size"):
            candidate = toolchain / name
            if candidate.is_file():
                return candidate
    for name in ("riscv64-unknown-elf-size", "riscv64-unknown-elf-size.exe"):
        candidate = shutil.which(name)
        if candidate:
            return Path(candidate)
    raise RuntimeError("riscv64-unknown-elf-size not found")


def parse_size_output(output: str) -> tuple[int, int, int]:
    lines = [line.split() for line in output.splitlines() if line.strip()]
    if len(lines) < 2 or lines[0][:3] != ["text", "data", "bss"]:
        raise ValueError("unexpected size output")
    try:
        text_bytes, data_bytes, bss_bytes = (int(value) for value in lines[1][:3])
    except (ValueError, IndexError) as exc:
        raise ValueError("unexpected size values") from exc
    return text_bytes, data_bytes, bss_bytes


def read_elf_size(elf: Path) -> tuple[int, int, int]:
    result = subprocess.run(
        [str(find_size()), str(elf)],
        check=True,
        text=True,
        capture_output=True,
    )
    return parse_size_output(result.stdout)


def _splice_translation_phases(source: str) -> str:
    """Apply C trigraph replacement and backslash-newline splicing."""

    source = source.replace("\r\n", "\n").replace("\r", "\n")
    result: list[str] = []
    index = 0
    while index < len(source):
        trigraph = TRIGRAPHS.get(source[index:index + 3])
        current = trigraph if trigraph is not None else source[index]
        consumed = 3 if trigraph is not None else 1
        if current == "\\":
            newline = index + consumed
            while newline < len(source) and source[newline] in " \t\f\v":
                newline += 1
            if newline < len(source) and source[newline] == "\n":
                index = newline + 1
                continue
        result.append(current)
        index += consumed
    return "".join(result)


def _strip_c_comments_and_literals(source: str) -> str:
    """Blank comments and literals while preserving offsets and line numbers."""

    source = _splice_translation_phases(source)
    result: list[str] = []
    index = 0
    state = "code"
    while index < len(source):
        current = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "line-comment":
            if current == "\n":
                result.append(current)
                state = "code"
            else:
                result.append(" ")
            index += 1
            continue
        if state == "block-comment":
            if current == "*" and following == "/":
                result.extend((" ", " "))
                index += 2
                state = "code"
            else:
                result.append("\n" if current == "\n" else " ")
                index += 1
            continue
        if state in {"string", "character"}:
            if current == "\\" and following:
                result.extend((" ", "\n" if following == "\n" else " "))
                index += 2
                continue
            terminator = '"' if state == "string" else "'"
            result.append("\n" if current == "\n" else " ")
            if current == terminator:
                state = "code"
            index += 1
            continue
        if current == "/" and following == "/":
            result.extend((" ", " "))
            index += 2
            state = "line-comment"
        elif current == "/" and following == "*":
            result.extend((" ", " "))
            index += 2
            state = "block-comment"
        else:
            if current == '"':
                state = "string"
                result.append(" ")
            elif current == "'":
                state = "character"
                result.append(" ")
            else:
                result.append(current)
            index += 1
    return "".join(result)


def _matching_brace(code: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(code) - 1


def _expression_fingerprint(code: str, start: int, *, limit: int = 240) -> str:
    """Return a normalized statement-sized fingerprint around one site."""

    end = start
    parens = 0
    while end < len(code) and end - start < limit:
        character = code[end]
        if character == "(":
            parens += 1
        elif character == ")" and parens:
            parens -= 1
        if character in ";\n" and parens == 0:
            break
        end += 1
    return re.sub(r"\s+", "", code[start:end])


def _site_context_fingerprint(code: str, start: int) -> str:
    """Fingerprint brace ancestry and the site's statement-line prefix."""

    stack: list[str] = []
    delimiter = 0
    for index, character in enumerate(code[:start]):
        if character == "{":
            header = re.sub(r"\s+", "", code[delimiter:index + 1])
            stack.append(header)
            delimiter = index + 1
        elif character == "}":
            if stack:
                stack.pop()
            delimiter = index + 1
        elif character == ";":
            delimiter = index + 1
    line_start = code.rfind("\n", 0, start) + 1
    line_prefix = re.sub(r"\s+", "", code[line_start:start])
    context = "|".join([*stack, f"line:{line_prefix}"])
    return hashlib.sha256(context.encode("utf-8")).hexdigest()


def _function_regions(code: str) -> list[tuple[str, int, int, int]]:
    regions: list[tuple[str, int, int, int]] = []
    occupied_until = -1
    for match in FUNCTION_RE.finditer(code):
        name = match.group("name")
        if name.rsplit("::", 1)[-1] in CONTROL_NAMES or match.start() < occupied_until:
            continue
        opening = code.find("{", match.start(), match.end())
        closing = _matching_brace(code, opening)
        regions.append((name, match.start(), opening + 1, closing))
        occupied_until = closing
    return regions


def _is_designated_initializer_alias(code: str, alias_start: int) -> bool:
    """Distinguish `.field = function` data from a function-pointer alias."""

    cursor = alias_start - 1
    while cursor >= 0 and code[cursor].isspace():
        cursor -= 1
    if cursor < 0 or code[cursor] != ".":
        return False
    cursor -= 1
    while cursor >= 0 and code[cursor].isspace():
        cursor -= 1
    return cursor >= 0 and code[cursor] in "{,"


def _runtime_sites(
    snapshot: dict[str, str],
    *,
    direct_runtime_calls: set[str] | None = None,
    wrapper_runtime_markers: tuple[str, ...] | None = None,
    include_cxx_new: bool = True,
    include_std_thread: bool = True,
) -> list[dict[str, str | int]]:
    """Conservatively find creation sites and their tainted wrappers.

    This is a lexical policy guard, not a full C/C++ semantic analyser.  It
    removes comments/literals, scans function bodies (not prototypes), follows
    explicit function aliases and wrapper calls, and fingerprints each site so
    a delete/add move cannot be hidden by an unchanged repository-wide count.
    """

    files: dict[str, tuple[str, str, list[tuple[str, int, int, int]]]] = {}
    functions: list[dict[str, object]] = []
    runtime_calls = (
        DIRECT_RUNTIME_CALLS
        if direct_runtime_calls is None else direct_runtime_calls
    )
    wrapper_markers = (
        WRAPPER_RUNTIME_MARKERS
        if wrapper_runtime_markers is None else wrapper_runtime_markers
    )
    primitive_names = {
        name.lstrip(":").rsplit("::", 1)[-1].lower()
        for name in runtime_calls
    }
    for relative, source in sorted(snapshot.items()):
        code = _strip_c_comments_and_literals(source)
        normalized_hash = hashlib.sha256(
            re.sub(r"\s+", "", code).encode("utf-8")
        ).hexdigest()
        regions = _function_regions(code)
        files[relative] = (code, normalized_hash, regions)
        for name, definition, body_start, body_end in regions:
            functions.append({
                "path": relative,
                "name": name,
                "definition": definition,
                "body_start": body_start,
                "body_end": body_end,
                "body": code[body_start:body_end],
            })

    aliases_by_file: dict[str, set[str]] = {}
    for relative, (code, _digest, _regions) in files.items():
        file_aliases: set[str] = set()
        for match in ALIAS_RE.finditer(code):
            if _is_designated_initializer_alias(code, match.start()):
                continue
            target = match.group("target").lstrip(":").rsplit("::", 1)[-1].lower()
            if target in primitive_names:
                file_aliases.add(match.group("alias"))
        for match in MACRO_RE.finditer(code):
            body_names = {
                token.lstrip(":").rsplit("::", 1)[-1].lower()
                for token in re.findall(
                    r"(?:::)?[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*",
                    match.group("body"),
                )
            }
            if body_names & primitive_names:
                file_aliases.add(match.group("name"))
        aliases_by_file[relative] = file_aliases
    # Headers and external function-pointer declarations can expose an alias to
    # another translation unit.  Conservatively treat explicit alias names as
    # repository-wide for use-site detection; definitions remain file-scoped
    # evidence sites below.
    global_aliases = {
        alias.lower()
        for aliases in aliases_by_file.values()
        for alias in aliases
    }

    tainted: set[str] = set()
    changed = True
    while changed:
        changed = False
        for function in functions:
            name = str(function["name"])
            body = str(function["body"])
            path = str(function["path"])
            known = primitive_names | {
                wrapper.lstrip(":").rsplit("::", 1)[-1].lower()
                for wrapper in tainted
            } | {
                alias.lower() for alias in aliases_by_file.get(path, set())
            }
            calls = {
                match.group("name").lstrip(":").rsplit("::", 1)[-1].lower()
                for match in CALL_RE.finditer(body)
            }
            if (
                calls & known
                or (include_cxx_new and CXX_NEW_RE.search(body))
                or (include_std_thread and STD_THREAD_OBJECT_RE.search(body))
            ):
                if name not in tainted:
                    tainted.add(name)
                    changed = True

    tainted_base = {name.lstrip(":").rsplit("::", 1)[-1].lower() for name in tainted}
    sites: list[dict[str, str | int]] = []

    def add_site(
        signature: str, relative: str, code: str, position: int,
        function: str, fingerprint: str,
    ) -> None:
        sites.append({
            "signature": signature,
            "path": relative,
            "line": code.count("\n", 0, position) + 1,
            "function": function,
            "fingerprint": fingerprint,
            "context": _site_context_fingerprint(code, position),
            "file_hash": files[relative][1],
        })

    for relative, (code, _digest, regions) in files.items():
        file_aliases = global_aliases
        for match in ALIAS_RE.finditer(code):
            if _is_designated_initializer_alias(code, match.start()):
                continue
            target = match.group("target").lstrip(":").rsplit("::", 1)[-1].lower()
            if target in primitive_names or target in tainted_base:
                add_site(
                    f"alias:{match.group('alias')}", relative, code, match.start(),
                    "<file>", _expression_fingerprint(code, match.start()),
                )
        for match in MACRO_RE.finditer(code):
            body_names = {
                token.lstrip(":").rsplit("::", 1)[-1].lower()
                for token in re.findall(
                    r"(?:::)?[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*",
                    match.group("body"),
                )
            }
            if body_names & primitive_names:
                add_site(
                    f"alias:{match.group('name')}", relative, code, match.start(),
                    "<macro>", re.sub(r"\s+", "", match.group(0)),
                )
        for name, definition, body_start, body_end in regions:
            body = code[body_start:body_end]
            for match in CALL_RE.finditer(body):
                raw_name = match.group("name")
                basename = raw_name.lstrip(":").rsplit("::", 1)[-1].lower()
                if (
                    basename not in primitive_names
                    and basename not in tainted_base
                    and basename not in file_aliases
                    and not any(marker in basename for marker in wrapper_markers)
                ):
                    continue
                absolute = body_start + match.start()
                add_site(
                    f"call:{raw_name}", relative, code, absolute, name,
                    _expression_fingerprint(code, absolute),
                )
            if include_cxx_new:
                for match in CXX_NEW_RE.finditer(body):
                    absolute = body_start + match.start()
                    add_site(
                        "cxx:new", relative, code, absolute, name,
                        _expression_fingerprint(code, absolute),
                    )
            if include_std_thread:
                for match in STD_THREAD_OBJECT_RE.finditer(body):
                    absolute = body_start + match.start()
                    add_site(
                        "call:std::thread", relative, code, absolute, name,
                        _expression_fingerprint(code, absolute),
                    )
        file_scope_matchers = []
        if include_cxx_new:
            file_scope_matchers.append((CXX_NEW_RE, "cxx:new"))
        if include_std_thread:
            file_scope_matchers.append((STD_THREAD_OBJECT_RE, "call:std::thread"))
        for matcher, signature in file_scope_matchers:
            for match in matcher.finditer(code):
                if any(body_start <= match.start() < body_end
                       for _name, _definition, body_start, body_end in regions):
                    continue
                add_site(
                    signature, relative, code, match.start(), "<file>",
                    _expression_fingerprint(code, match.start()),
                )
        for match in CALL_RE.finditer(code):
            if any(body_start <= match.start() < body_end
                   for _name, _definition, body_start, body_end in regions):
                continue
            raw_name = match.group("name")
            basename = raw_name.lstrip(":").rsplit("::", 1)[-1].lower()
            if basename in CONTROL_NAMES:
                continue
            if (
                basename not in primitive_names
                and basename not in tainted_base
                and basename not in file_aliases
                and not any(marker in basename for marker in wrapper_markers)
            ):
                continue
            add_site(
                f"call:{raw_name}", relative, code, match.start(), "<file>",
                _expression_fingerprint(code, match.start()),
            )
    return sites


def _runtime_occurrences(
    snapshot: dict[str, str],
) -> dict[str, list[str]]:
    occurrences: dict[str, list[str]] = {}
    for site in _runtime_sites(snapshot):
        signature = str(site["signature"])
        occurrences.setdefault(signature, []).append(
            f"{site['path']}:{site['line']}"
        )
    return occurrences


def _baseline_source_snapshot(commit: str, *, root: Path) -> dict[str, str]:
    top_level = subprocess.run(
        ["git", "ls-tree", "-d", "--name-only", commit],
        cwd=root,
        check=True,
        text=True,
        capture_output=True,
    ).stdout.splitlines()
    scopes = [scope for scope in SOURCE_ROOTS if scope in top_level]
    if not scopes:
        return {}
    archive = subprocess.run(
        ["git", "archive", "--format=tar", commit, "--", *scopes],
        cwd=root,
        check=True,
        capture_output=True,
    ).stdout
    snapshot: dict[str, str] = {}
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as bundle:
        for member in bundle.getmembers():
            if not member.isfile() or Path(member.name).suffix.lower() not in SOURCE_SUFFIXES:
                continue
            source = bundle.extractfile(member)
            if source is not None:
                snapshot[Path(member.name).as_posix()] = source.read().decode(
                    "utf-8", errors="replace"
                )
    return snapshot


def _current_source_snapshot(*, root: Path) -> dict[str, str]:
    snapshot: dict[str, str] = {}
    for scope in SOURCE_ROOTS:
        directory = root / scope
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            relative = path.relative_to(root).as_posix()
            snapshot[relative] = path.read_text(encoding="utf-8", errors="replace")
    return snapshot


def added_runtime_objects_from_snapshots(
    baseline_snapshot: dict[str, str],
    current_snapshot: dict[str, str],
    *,
    direct_runtime_calls: set[str] | None = None,
    wrapper_runtime_markers: tuple[str, ...] | None = None,
    include_cxx_new: bool = True,
    include_std_thread: bool = True,
    site_line_sensitive: bool = True,
    site_context_sensitive: bool = True,
) -> list[str]:
    """Report creation sites not grandfathered by an exact source snapshot."""

    scanner_options = {
        "direct_runtime_calls": direct_runtime_calls,
        "wrapper_runtime_markers": wrapper_runtime_markers,
        "include_cxx_new": include_cxx_new,
        "include_std_thread": include_std_thread,
    }
    baseline = _runtime_sites(baseline_snapshot, **scanner_options)
    current = _runtime_sites(current_snapshot, **scanner_options)
    baseline_file_paths: dict[str, set[str]] = {}
    for site in baseline:
        baseline_file_paths.setdefault(str(site["file_hash"]), set()).add(
            str(site["path"])
        )

    def identity(
        site: dict[str, str | int], path: str
    ) -> tuple[object, ...]:
        signature = str(site["signature"])
        function = str(site["function"])
        fingerprint = str(site["fingerprint"])
        context = str(site["context"])
        common = (signature, path, function, fingerprint)
        if site_context_sensitive:
            common += (context,)
        return common + (int(site["line"]),) if site_line_sensitive else common

    def keys(site: dict[str, str | int]) -> set[tuple[object, ...]]:
        paths = {str(site["path"])}
        # Exact normalized file content may be renamed without manufacturing a
        # new runtime object. Modified/copy-pasted files do not get this waiver.
        paths.update(baseline_file_paths.get(str(site["file_hash"]), set()))
        return {identity(site, path) for path in paths}

    baseline_keys = Counter(
        identity(site, str(site["path"]))
        for site in baseline
    )
    findings: list[str] = []
    for site in current:
        matches = [key for key in keys(site) if baseline_keys[key] > 0]
        if matches:
            baseline_keys[matches[0]] -= 1
            continue
        findings.append(
            f"{site['signature']} introduced at {site['path']}:{site['line']} "
            f"in {site['function']}"
        )
    return sorted(findings)


def added_runtime_objects(commit: str, *, root: Path = ROOT) -> list[str]:
    """Report current creation sites not grandfathered by a baseline site."""

    return added_runtime_objects_from_snapshots(
        _baseline_source_snapshot(commit, root=root),
        _current_source_snapshot(root=root),
    )


def artifact_paths(board_id: str = BOARD_ID) -> dict[str, Path]:
    root = ROOT / "build" / board_id
    return {
        "image": root / "hackylens-full.bin",
        "elf": root / "sdk-full" / "hackylens_full",
        "composition": root / "composition.json",
        "capabilities": root / "capabilities.json",
        "attestation": root / "hackylens-full.attestation.json",
    }

def artifact_measurement(paths: dict[str, Path]) -> dict[str, Any]:
    for label, path in paths.items():
        if not path.is_file():
            raise RuntimeError(f"{label} artifact is missing: {path}")
    text_bytes, data_bytes, bss_bytes = read_elf_size(
        paths["elf"]
    )
    board = load_board(BOARD_ID)
    flash = board.flash
    raw_bytes = paths["image"].stat().st_size
    rounded = math.ceil(
        (raw_bytes + build_firmware.K210_IMAGE_OVERHEAD) / flash["erase_size"]
    ) * flash["erase_size"]
    return {
        "attestation_sha256": sha256(paths["attestation"]),
        "capabilities_sha256": sha256(paths["capabilities"]),
        "composition_sha256": sha256(paths["composition"]),
        "elf": {
            "bss_bytes": bss_bytes,
            "data_bytes": data_bytes,
            "sha256": sha256(paths["elf"]),
            "static_ram_bytes": data_bytes + bss_bytes,
            "text_bytes": text_bytes,
        },
        "image": {
            "flash_occupied_bytes": rounded,
            "raw_bytes": raw_bytes,
            "sha256": sha256(paths["image"]),
        },
    }

def _new_direct_resources(baseline_commit: str) -> list[str]:
    findings = added_runtime_objects(baseline_commit)
    direct: list[str] = []
    for finding in findings:
        signature = finding.split(" introduced at ", 1)[0]
        if signature == "cxx:new" or signature.startswith("alias:"):
            direct.append(finding)
            continue
        if not signature.startswith("call:"):
            continue
        basename = signature[5:].lstrip(":").rsplit("::", 1)[-1].casefold()
        if basename in DIRECT_RUNTIME_CALLS:
            direct.append(finding)
    return sorted(direct)
