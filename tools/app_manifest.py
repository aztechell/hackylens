#!/usr/bin/env python3
"""Strict schema-1 Native App Manifest validation and canonicalization."""

from __future__ import annotations

import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import tomllib
from typing import Any, Mapping, Sequence


SCHEMA_MAJOR = 1
APP_ID_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
CAPABILITY_ID_RE = re.compile(
    r"^hackylens\.cap\.[a-z][a-z0-9]*(?:-[a-z0-9]+)*$"
)
SERVICE_ID_RE = re.compile(
    r"^hackylens\.service\.[a-z][a-z0-9]*(?:-[a-z0-9]+)*$"
)
TOKEN_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
C_SYMBOL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
RELEASE_VERSION_RE = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$"
)
APP_VERSION_RE = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)
PATH_SEGMENT_RE = re.compile(r"^[A-Za-z0-9_.-]+$")

ROOT_FIELDS = {
    "schema", "id", "name", "version", "entry", "generated_symbol",
    "lifecycle", "sources", "private_includes", "menu", "autostart",
    "capabilities", "services", "limits", "metadata", "tests",
}
MENU_FIELDS = {"visible", "order"}
AUTOSTART_FIELDS = {"eligible", "id"}
CAPABILITIES_FIELDS = {"required", "optional"}
CAPABILITY_FIELDS = {
    "id", "instance", "minimum", "maximum_exclusive", "features",
}
OPTIONAL_CAPABILITY_FIELDS = CAPABILITY_FIELDS | {"fallback"}
SERVICE_FIELDS = {"id", "namespace"}
LIMIT_FIELDS = {
    "static_ram_bytes", "stack_bytes", "state_bytes", "tick_interval_us",
    "tick_budget_us", "render_budget_us",
}
METADATA_FIELDS = {"help", "debug"}
TEST_FIELDS = {"host_sources", "build_profiles"}

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
HOST_TEST_SUFFIXES = SOURCE_SUFFIXES | {".py"}
BUILD_PROFILES = {"standalone", "full", "disabled"}
LIFECYCLE_KINDS = {"legacy", "v2"}

MAX_APP_ID_BYTES = 63
MAX_DISPLAY_NAME_BYTES = 96
MAX_METADATA_BYTES = 1024
MAX_STATIC_RAM_BYTES = 8 * 1024 * 1024
MAX_STACK_BYTES = 32 * 1024
MAX_STATE_BYTES = 1024 * 1024
MAX_TICK_INTERVAL_US = 60_000_000
MAX_TICK_BUDGET_US = 1_000_000
MAX_RENDER_BUDGET_US = 1_000_000


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


def _mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise ManifestError(f"{label}: expected table")
    return value


def _strict(value: Any, fields: set[str], label: str) -> Mapping[str, Any]:
    table = _mapping(value, label)
    unknown = sorted(set(table) - fields)
    missing = sorted(fields - set(table))
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


def _boolean(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise ManifestError(f"{label}: expected boolean")
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


def _release_version(value: Any, label: str) -> tuple[str, tuple[int, int, int]]:
    text = _string(value, label)
    match = RELEASE_VERSION_RE.fullmatch(text)
    if match is None:
        raise ManifestError(f"{label}: expected canonical MAJOR.MINOR.PATCH")
    components = (
        int(match.group(1)), int(match.group(2)), int(match.group(3))
    )
    if any(part > 0xFFFF for part in components):
        raise ManifestError(f"{label}: component exceeds uint16")
    return text, components


def _app_version(value: Any, label: str) -> str:
    text = _string(value, label)
    match = APP_VERSION_RE.fullmatch(text)
    if match is None:
        raise ManifestError(f"{label}: expected canonical SemVer")
    prerelease = match.group(4)
    if prerelease is not None:
        for identifier in prerelease.split("."):
            if identifier.isdigit() and len(identifier) > 1 and identifier[0] == "0":
                raise ManifestError(
                    f"{label}: numeric prerelease identifiers cannot have leading zeroes"
                )
    return text


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


def _capability_request(value: Any, label: str, *, optional: bool) -> dict[str, Any]:
    fields = OPTIONAL_CAPABILITY_FIELDS if optional else CAPABILITY_FIELDS
    table = _strict(value, fields, label)
    minimum, minimum_value = _release_version(table["minimum"], f"{label}.minimum")
    maximum, maximum_value = _release_version(
        table["maximum_exclusive"], f"{label}.maximum_exclusive"
    )
    if minimum_value >= maximum_value:
        raise ManifestError(f"{label}: minimum must precede maximum_exclusive")
    features = [
        _string(item, f"{label}.features[{index}]", pattern=TOKEN_RE)
        for index, item in enumerate(_array(table["features"], f"{label}.features"))
    ]
    if len(features) != len(set(features)):
        raise ManifestError(f"{label}.features: duplicate value")
    result: dict[str, Any] = {
        "id": _string(table["id"], f"{label}.id", pattern=CAPABILITY_ID_RE),
        "instance": _integer(table["instance"], f"{label}.instance", 0, 0xFFFF),
        "minimum": minimum,
        "maximum_exclusive": maximum,
        "features": sorted(features),
    }
    if optional:
        result["fallback"] = _string(
            table["fallback"], f"{label}.fallback", pattern=TOKEN_RE
        )
    return result


def _capability_array(value: Any, label: str, *, optional: bool) -> list[dict[str, Any]]:
    result = [
        _capability_request(item, f"{label}[{index}]", optional=optional)
        for index, item in enumerate(_array(value, label))
    ]
    keys = [(item["id"], item["instance"]) for item in result]
    if len(keys) != len(set(keys)):
        raise ManifestError(f"{label}: duplicate capability request")
    return sorted(result, key=lambda item: (item["id"], item["instance"]))


def _services(value: Any, label: str, app_id: str) -> list[dict[str, str]]:
    result: list[dict[str, str]] = []
    for index, item in enumerate(_array(value, label)):
        item_label = f"{label}[{index}]"
        table = _strict(item, SERVICE_FIELDS, item_label)
        service_id = _string(table["id"], f"{item_label}.id", pattern=SERVICE_ID_RE)
        namespace = _string(table["namespace"], f"{item_label}.namespace")
        if not namespace.startswith(app_id + "."):
            raise ManifestError(
                f"{item_label}.namespace: must be scoped below {app_id!r}"
            )
        suffix = namespace[len(app_id) + 1:]
        if not suffix or any(TOKEN_RE.fullmatch(part) is None for part in suffix.split(".")):
            raise ManifestError(f"{item_label}.namespace: invalid scoped namespace")
        result.append({"id": service_id, "namespace": namespace})
    ids = [item["id"] for item in result]
    namespaces = [item["namespace"] for item in result]
    if len(ids) != len(set(ids)) or len(namespaces) != len(set(namespaces)):
        raise ManifestError(f"{label}: duplicate service or namespace")
    return sorted(result, key=lambda item: (item["id"], item["namespace"]))


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
    table = _strict(raw, ROOT_FIELDS, f"{path}: root")
    if isinstance(table["schema"], bool) or table["schema"] != SCHEMA_MAJOR:
        raise ManifestError(f"{path}: schema must be integer {SCHEMA_MAJOR}")

    app_id = _string(
        table["id"], f"{path}: id", pattern=APP_ID_RE,
        maximum_bytes=MAX_APP_ID_BYTES,
    )
    sources = _path_array(
        table["sources"], f"{path}: sources", directory_real,
        expected="file", suffixes=SOURCE_SUFFIXES, non_empty=True,
    )
    private_includes = _path_array(
        table["private_includes"], f"{path}: private_includes", directory_real,
        expected="directory", non_empty=False,
    )

    menu = _strict(table["menu"], MENU_FIELDS, f"{path}: menu")
    autostart = _strict(
        table["autostart"], AUTOSTART_FIELDS, f"{path}: autostart"
    )
    autostart_eligible = _boolean(
        autostart["eligible"], f"{path}: autostart.eligible"
    )
    autostart_id = _integer(
        autostart["id"], f"{path}: autostart.id", 0, 0xFFFF
    )
    if autostart_eligible != (autostart_id != 0):
        raise ManifestError(
            f"{path}: autostart.id must be non-zero exactly when eligible"
        )

    capabilities = _strict(
        table["capabilities"], CAPABILITIES_FIELDS, f"{path}: capabilities"
    )
    required = _capability_array(
        capabilities["required"], f"{path}: capabilities.required", optional=False
    )
    optional = _capability_array(
        capabilities["optional"], f"{path}: capabilities.optional", optional=True
    )
    capability_keys = [
        (item["id"], item["instance"]) for item in required + optional
    ]
    if len(capability_keys) != len(set(capability_keys)):
        raise ManifestError(
            f"{path}: capability cannot be both required and optional"
        )

    limits = _strict(table["limits"], LIMIT_FIELDS, f"{path}: limits")
    static_ram = _integer(
        limits["static_ram_bytes"], f"{path}: limits.static_ram_bytes",
        1, MAX_STATIC_RAM_BYTES,
    )
    stack = _integer(
        limits["stack_bytes"], f"{path}: limits.stack_bytes", 1, MAX_STACK_BYTES
    )
    state = _integer(
        limits["state_bytes"], f"{path}: limits.state_bytes", 1, MAX_STATE_BYTES
    )
    tick_interval = _integer(
        limits["tick_interval_us"], f"{path}: limits.tick_interval_us",
        1, MAX_TICK_INTERVAL_US,
    )
    tick_budget = _integer(
        limits["tick_budget_us"], f"{path}: limits.tick_budget_us",
        1, MAX_TICK_BUDGET_US,
    )
    render_budget = _integer(
        limits["render_budget_us"], f"{path}: limits.render_budget_us",
        1, MAX_RENDER_BUDGET_US,
    )
    if state > static_ram:
        raise ManifestError(f"{path}: limits.state_bytes exceeds static_ram_bytes")
    if tick_budget > tick_interval:
        raise ManifestError(f"{path}: limits.tick_budget_us exceeds tick_interval_us")

    metadata = _strict(table["metadata"], METADATA_FIELDS, f"{path}: metadata")
    tests = _strict(table["tests"], TEST_FIELDS, f"{path}: tests")
    host_sources = _path_array(
        tests["host_sources"], f"{path}: tests.host_sources", directory_real,
        expected="file", suffixes=HOST_TEST_SUFFIXES, non_empty=True,
    )
    profiles = [
        _string(item, f"{path}: tests.build_profiles[{index}]", pattern=TOKEN_RE)
        for index, item in enumerate(
            _array(tests["build_profiles"], f"{path}: tests.build_profiles")
        )
    ]
    if not profiles:
        raise ManifestError(f"{path}: tests.build_profiles must not be empty")
    if len(profiles) != len(set(profiles)):
        raise ManifestError(f"{path}: tests.build_profiles has duplicate value")
    unknown_profiles = sorted(set(profiles) - BUILD_PROFILES)
    if unknown_profiles:
        raise ManifestError(
            f"{path}: tests.build_profiles unknown={','.join(unknown_profiles)}"
        )

    return {
        "directory": directory,
        "schema": SCHEMA_MAJOR,
        "id": app_id,
        "name": _string(
            table["name"], f"{path}: name", maximum_bytes=MAX_DISPLAY_NAME_BYTES
        ),
        "version": _app_version(table["version"], f"{path}: version"),
        "entry": _string(table["entry"], f"{path}: entry", pattern=C_SYMBOL_RE),
        "generated_symbol": _string(
            table["generated_symbol"], f"{path}: generated_symbol",
            pattern=C_SYMBOL_RE,
        ),
        "lifecycle": _string(
            table["lifecycle"], f"{path}: lifecycle", pattern=TOKEN_RE
        ),
        "sources": sources,
        "private_includes": private_includes,
        "menu": {
            "visible": _boolean(menu["visible"], f"{path}: menu.visible"),
            "order": _integer(menu["order"], f"{path}: menu.order", 1, 0xFFFF),
        },
        "autostart": {"eligible": autostart_eligible, "id": autostart_id},
        "capabilities": {"required": required, "optional": optional},
        "services": _services(table["services"], f"{path}: services", app_id),
        "limits": {
            "static_ram_bytes": static_ram,
            "stack_bytes": stack,
            "state_bytes": state,
            "tick_interval_us": tick_interval,
            "tick_budget_us": tick_budget,
            "render_budget_us": render_budget,
        },
        "metadata": {
            "help": _string(
                metadata["help"], f"{path}: metadata.help",
                maximum_bytes=MAX_METADATA_BYTES,
            ),
            "debug": _string(
                metadata["debug"], f"{path}: metadata.debug",
                maximum_bytes=MAX_METADATA_BYTES,
            ),
        },
        "tests": {
            "host_sources": host_sources,
            "build_profiles": sorted(profiles),
        },
    }


def canonical_model(manifests: Sequence[dict[str, Any]]) -> dict[str, Any]:
    """Collision-check manifests and return their stable canonical ordering."""

    if not manifests:
        raise ManifestError("no app.toml manifests found")
    collision_fields = {
        "id": [(item["id"], item["id"]) for item in manifests],
        "entry": [(item["entry"], item["id"]) for item in manifests],
        "generated_symbol": [
            (item["generated_symbol"], item["id"]) for item in manifests
        ],
        "menu.order": [(item["menu"]["order"], item["id"]) for item in manifests],
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
    for manifest in manifests:
        if manifest["lifecycle"] not in LIFECYCLE_KINDS:
            raise ManifestError(
                f"{manifest['id']}: lifecycle must be one of {sorted(LIFECYCLE_KINDS)}"
            )
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
