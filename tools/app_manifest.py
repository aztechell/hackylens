#!/usr/bin/env python3
"""Validate Native App Manifests and emit one canonical build model."""

from __future__ import annotations

import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import tomllib
from typing import Any, Mapping, Sequence


SCHEMA_MAJOR = 1
APP_ID_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
TOKEN_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
C_SYMBOL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
PATH_SEGMENT_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
FIRMWARE_SERVICE_PREFIX = "hackylens.firmware."

REQUIRED_FIELDS = {
    "id", "name", "entry", "sources", "requires", "tick_ms",
}
OPTIONAL_FIELDS = {
    "private_includes", "menu_order", "autostart_id", "optional", "debug", "tick_budget_ms", "debug_entry",
}
ALLOWED_FIELDS = REQUIRED_FIELDS | OPTIONAL_FIELDS
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}

DESCRIPTOR_VERSION = "0.1.0"
PLACEHOLDER_STATIC_RAM_BYTES = 1_048_576
PLACEHOLDER_STACK_BYTES = 16_384
PLACEHOLDER_STATE_BYTES = 262_144
V2_STATE_BYTES = 1_024
# Runtime presents with this deadline. 10 ms cannot finish a K210 320x240 SPI
# transfer; the display adapter and UI already use 500 ms.
PLACEHOLDER_RENDER_BUDGET_US = 500_000
DEFAULT_DEBUG = "No app-specific debug commands."

MAX_APP_ID_BYTES = 63
MAX_DISPLAY_NAME_BYTES = 96
MAX_DEBUG_BYTES = 1024
MAX_TICK_MS = 60_000
HARDWARE_SERVICES = frozenset({"display", "input", "time", "lights", "external-link"})
FIRMWARE_SERVICES = frozenset({"camera", "sd-card", "internal-flash", "settings"})
OPTIONAL_FALLBACKS = {"display": "headless", "external-link": "hide-external-link-menu"}


class ManifestError(ValueError):
    """A Native App Manifest is malformed or unsafe."""


def canonical_json_bytes(document: object) -> bytes:
    return (
        json.dumps(
            document,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")


def generated_symbol(app_id: str) -> str:
    return "hk_generated_app_" + app_id.replace("-", "_")


def _mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise ManifestError(f"{label}: expected table")
    return value


def _allowed(value: Any, allowed: set[str], required: set[str], label: str) -> Mapping[str, Any]:
    table = _mapping(value, label)
    unknown = sorted(set(table) - allowed)
    missing = sorted(required - set(table))
    if unknown or missing:
        details: list[str] = []
        if unknown:
            details.append("unknown=" + ",".join(unknown))
        if missing:
            details.append("missing=" + ",".join(missing))
        raise ManifestError(f"{label}: " + "; ".join(details))
    return table


def _string(
    value: Any,
    label: str,
    *,
    pattern: re.Pattern[str] | None = None,
    maximum_bytes: int | None = None,
) -> str:
    if not isinstance(value, str) or not value or value != value.strip():
        raise ManifestError(f"{label}: expected non-empty trimmed string")
    if any(ord(character) < 0x20 for character in value):
        raise ManifestError(f"{label}: control characters are forbidden")
    if pattern is not None and pattern.fullmatch(value) is None:
        raise ManifestError(f"{label}: invalid value {value!r}")
    if maximum_bytes is not None and len(value.encode("utf-8")) > maximum_bytes:
        raise ManifestError(f"{label}: exceeds {maximum_bytes} UTF-8 bytes")
    return value


def _integer(value: Any, label: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ManifestError(f"{label}: expected integer")
    if value < minimum or value > maximum:
        raise ManifestError(f"{label}: outside {minimum}..{maximum}")
    return value


def _array(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ManifestError(f"{label}: expected array")
    return value


def _name_array(value: Any, label: str, *, allow_empty: bool) -> list[str]:
    raw = _array(value, label)
    if not raw:
        if allow_empty:
            return []
        raise ManifestError(f"{label}: must not be empty")
    names = [
        _string(item, f"{label}[{index}]", pattern=TOKEN_RE)
        for index, item in enumerate(raw)
    ]
    if len(names) != len(set(names)):
        raise ManifestError(f"{label}: duplicate value")
    return names


def _portable_parts(value: Any, label: str) -> tuple[str, ...]:
    text = _string(value, label)
    if "\\" in text or ":" in text:
        raise ManifestError(f"{label}: use a canonical forward-slash relative path")
    if PurePosixPath(text).is_absolute() or PureWindowsPath(text).is_absolute():
        raise ManifestError(f"{label}: absolute paths are forbidden")
    raw_parts = text.split("/")
    if any(
        not part or part in {".", ".."} or PATH_SEGMENT_RE.fullmatch(part) is None
        for part in raw_parts
    ):
        raise ManifestError(f"{label}: path is not canonical or contains traversal")
    return tuple(raw_parts)


def _resolve_existing(path: Path) -> Path:
    return path.resolve(strict=True)


def _resolve_app_path(
    app_directory: Path,
    value: Any,
    label: str,
    *,
    expected: str,
    suffixes: set[str] | None = None,
) -> str:
    parts = _portable_parts(value, label)
    cursor = app_directory
    for part in parts:
        try:
            names = {child.name for child in cursor.iterdir()}
        except OSError as exc:
            raise ManifestError(f"{label}: cannot inspect path: {exc}") from exc
        if part not in names:
            if any(name.casefold() == part.casefold() for name in names):
                raise ManifestError(f"{label}: path case does not match the filesystem")
            raise ManifestError(f"{label}: path does not exist")
        cursor = cursor / part
    try:
        app_real = _resolve_existing(app_directory)
        target_real = _resolve_existing(cursor)
        target_real.relative_to(app_real)
    except (OSError, ValueError) as exc:
        raise ManifestError(f"{label}: path escapes the real app directory") from exc
    if expected == "file" and not target_real.is_file():
        raise ManifestError(f"{label}: expected file")
    if expected == "directory" and not target_real.is_dir():
        raise ManifestError(f"{label}: expected directory")
    if suffixes is not None and target_real.suffix.casefold() not in suffixes:
        allowed = ", ".join(sorted(suffixes))
        raise ManifestError(f"{label}: expected one of {allowed}")
    return PurePosixPath(*parts).as_posix()


def _path_array(
    value: Any,
    label: str,
    app_directory: Path,
    *,
    expected: str,
    suffixes: set[str] | None = None,
    non_empty: bool,
) -> list[str]:
    raw = _array(value, label)
    if non_empty and not raw:
        raise ManifestError(f"{label}: must not be empty")
    if not non_empty and not raw:
        raise ManifestError(f"{label}: omit empty arrays")
    paths = [
        _resolve_app_path(
            app_directory,
            item,
            f"{label}[{index}]",
            expected=expected,
            suffixes=suffixes,
        )
        for index, item in enumerate(raw)
    ]
    folded = [item.casefold() for item in paths]
    if len(folded) != len(set(folded)):
        raise ManifestError(f"{label}: duplicate or case-colliding path")
    return sorted(paths, key=lambda item: (item.casefold(), item))


def load_manifest(path: Path, scan_root: Path) -> dict[str, Any]:
    """Load one manifest and return its path-independent canonical model."""

    if path.name != "app.toml":
        raise ManifestError(f"{path}: manifest must be named app.toml")
    try:
        root_real = _resolve_existing(scan_root)
        manifest_real = _resolve_existing(path)
        directory_real = manifest_real.parent
        directory = directory_real.relative_to(root_real).as_posix() or "."
        with path.open("rb") as source:
            raw = tomllib.load(source)
    except (OSError, ValueError, tomllib.TOMLDecodeError) as exc:
        raise ManifestError(f"cannot read manifest {path}: {exc}") from exc
    table = _allowed(raw, ALLOWED_FIELDS, REQUIRED_FIELDS, f"{path}: root")

    app_id = _string(
        table["id"], f"{path}: id", pattern=APP_ID_RE,
        maximum_bytes=MAX_APP_ID_BYTES,
    )
    name = _string(
        table["name"], f"{path}: name", maximum_bytes=MAX_DISPLAY_NAME_BYTES
    )
    sources = _path_array(
        table["sources"], f"{path}: sources", directory_real,
        expected="file", suffixes=SOURCE_SUFFIXES, non_empty=True,
    )
    if "private_includes" in table:
        private_includes = _path_array(
            table["private_includes"], f"{path}: private_includes",
            directory_real, expected="directory", non_empty=False,
        )
    else:
        private_includes = []

    if "menu_order" in table:
        menu = {
            "visible": True,
            "order": _integer(table["menu_order"], f"{path}: menu_order", 1, 0xFFFF),
        }
    else:
        menu = {"visible": False, "order": 0}

    if "autostart_id" in table:
        autostart_id = _integer(
            table["autostart_id"], f"{path}: autostart_id", 0, 0xFFFF
        )
    else:
        autostart_id = 0
    autostart = {"eligible": autostart_id != 0, "id": autostart_id}

    tick_ms = _integer(table["tick_ms"], f"{path}: tick_ms", 1, MAX_TICK_MS)
    tick_interval_us = tick_ms * 1000
    # Cadence and maximum callback duration are independent on cooperative hardware.
    tick_budget_us = _integer(
        table.get("tick_budget_ms", tick_ms), f"{path}: tick_budget_ms", 1, 5000
    ) * 1000

    required_names = _name_array(
        table["requires"], f"{path}: requires", allow_empty=True
    )
    optional_names = (
        _name_array(table["optional"], f"{path}: optional", allow_empty=False)
        if "optional" in table else []
    )
    overlap = sorted(set(required_names) & set(optional_names))
    if overlap:
        raise ManifestError(
            f"{path}: capability cannot be both required and optional"
        )
    for service_name in required_names + optional_names:
        if service_name not in HARDWARE_SERVICES | FIRMWARE_SERVICES:
            raise ManifestError(f"{path}: unknown required service {service_name!r}")
    for service_name in optional_names:
        if service_name in FIRMWARE_SERVICES:
            raise ManifestError(f"{path}: services cannot be optional")
        if service_name not in OPTIONAL_FALLBACKS:
            raise ManifestError(f"optional {service_name!r} has no build-time fallback")

    debug = (
        _string(
            table["debug"], f"{path}: debug", maximum_bytes=MAX_DEBUG_BYTES
        )
        if "debug" in table else DEFAULT_DEBUG
    )
    help_text = f"{name} feature app."

    return {
        "directory": directory,
        "schema": SCHEMA_MAJOR,
        "id": app_id,
        "name": name,
        "version": DESCRIPTOR_VERSION,
        "entry": _string(table["entry"], f"{path}: entry", pattern=C_SYMBOL_RE),
        "generated_symbol": generated_symbol(app_id),
        "sources": sources,
        "private_includes": private_includes,
        "menu": menu,
        "autostart": autostart,
        "requires": sorted(required_names),
        "optional": sorted(optional_names),
        "limits": {
            "static_ram_bytes": PLACEHOLDER_STATIC_RAM_BYTES,
            "stack_bytes": PLACEHOLDER_STACK_BYTES,
            "state_bytes": V2_STATE_BYTES,
            "tick_interval_us": tick_interval_us,
            "tick_budget_us": tick_budget_us,
            "render_budget_us": PLACEHOLDER_RENDER_BUDGET_US,
        },
        "metadata": {"help": help_text, "debug": debug,
                     "debug_entry": _string(table["debug_entry"], f"{path}: debug_entry", pattern=C_SYMBOL_RE)
                     if "debug_entry" in table else None},
    }


def canonical_model(manifests: Sequence[dict[str, Any]]) -> dict[str, Any]:
    """Collision-check manifests and return their stable canonical ordering."""

    if not manifests:
        raise ManifestError("no app.toml manifests found")
    collision_fields = {
        "id": [(item["id"], item["id"]) for item in manifests],
        "entry": [(item["entry"], item["id"]) for item in manifests],
        "menu.order": [
            (item["menu"]["order"], item["id"])
            for item in manifests if item["menu"]["visible"]
        ],
        "autostart.id": [
            (item["autostart"]["id"], item["id"])
            for item in manifests if item["autostart"]["eligible"]
        ],
    }
    for field, values in collision_fields.items():
        owners: dict[Any, str] = {}
        for value, app_id in values:
            if value in owners:
                raise ManifestError(
                    f"collision for {field}={value!r}: {owners[value]!r}, {app_id!r}"
                )
            owners[value] = app_id
    return {
        "schema": SCHEMA_MAJOR,
        "apps": sorted(manifests, key=lambda item: item["id"]),
    }


def validate_tree(scan_root: Path) -> dict[str, Any]:
    """Validate every app.toml below a root and return one canonical model."""

    if not scan_root.is_dir():
        raise ManifestError(f"scan root does not exist: {scan_root}")
    paths = sorted(scan_root.rglob("app.toml"), key=lambda item: item.as_posix())
    return canonical_model([load_manifest(path, scan_root) for path in paths])
